/*
 * E1000.c — Intel e1000 / e1000e（PR-H4 + H4e-1/2/3）
 *
 * QEMU: -device e1000 | -device e1000e
 * 同步 TX + RX ring；H4e-3 试 MSI RX（失败则仍 poll，NetPoll 备份）。
 * 无卡或无链路时 Setup 失败，不挡桌面。
 */

#include "E1000.h"
#include "E1000Private.h"
#include "PCIe.h"
#include "PhysicalMemory.h"
#include "VirtualMemory.h"
#include "Debug.h"
#include "Hal.h"
#include "HalPort.h"
#include "HalSerial.h"
#include "Net.h"
#include "ToySerialLog.h"

volatile UINT8 *gBar;
UINT64 gBarPhys;
UINT16 gPciDid;
UINT8 gPciBus;
UINT8 gPciDev;
UINT8 gPciFn;
static E1000_RX_DESC *gRxRing;
static E1000_TX_DESC *gTxRing;
static UINT8 *gRxBufs;
static UINT8 *gTxBuf;
static UINT16 gRxTail;
static UINT16 gTxTail;
UINT8 gE1000Mac[6];
static int gReady;
int gE1000UseIrq; /* PR-H4e-3：MSI 武装成功 */
static volatile UINT32 gStatIrq;

int E1000Ready(void) {
    return gReady;
}

void E1000GetMac(UINT8 Mac[6]) {
    CopyMemory(Mac, gE1000Mac, 6);
}

/*
 * PR-H4e-2：读 STATUS 链路/速度/双工（8254x / 82574 编码一致）。
 * 成功 0；未就绪 -1。Mbps=0 表示未知。
 */
int E1000GetLink(int *UpOut, UINT32 *MbpsOut, int *FullDuplexOut) {
    UINT32 St;
    UINT32 Sp;
    UINT32 Mbps = 0;

    if (!gReady || !gBar) {
        return -1;
    }
    St = MmioR32(E1000_REG_STATUS);
    Sp = (St & E1000_STATUS_SPEED_MASK) >> E1000_STATUS_SPEED_SHIFT;
    if (Sp == 0u) {
        Mbps = 10;
    } else if (Sp == 1u) {
        Mbps = 100;
    } else if (Sp == 2u) {
        Mbps = 1000;
    }
    if (UpOut) {
        *UpOut = (St & E1000_STATUS_LU) ? 1 : 0;
    }
    if (MbpsOut) {
        *MbpsOut = Mbps;
    }
    if (FullDuplexOut) {
        *FullDuplexOut = (St & E1000_STATUS_FD) ? 1 : 0;
    }
    return 0;
}

/* 82574 → "e1000e"；I219 → "i219"；其它 → "e1000" */
const char *E1000ChipName(void) {
    if (gPciDid == E1000_DID_82574L) {
        return "e1000e";
    }
    if (gPciDid == E1000_DID_I219_LM) {
        return "i219";
    }
    return "e1000";
}

int E1000Setup(void) {
    UINT8 Bus;
    UINT8 Dev;
    UINT8 Fn;
    UINT64 Bar;
    UINT16 Did = 0;
    UINT8 *Mem;
    UINT64 Phys;
    UINT32 i;
    int Spin;

    if (gReady) {
        return 1;
    }
    if (!VirtualMemoryEnabled()) {
        return 0;
    }
    if (!PciFindE1000(&Bus, &Dev, &Fn, &Bar, &Did)) {
        return 0;
    }
    gPciDid = Did;
    gPciBus = Bus;
    gPciDev = Dev;
    gPciFn = Fn;

    if (VirtualMemoryMapRange(Bar, Bar, 0x20000, PTE_PRESENT | PTE_WRITABLE) != 0) {
        DebugWrite("e1000: map BAR failed\n");
        return 0;
    }
    gBarPhys = Bar;
    gBar = (volatile UINT8 *)(UINTN)Bar;

    /* 复位；IMC 全掩；链路起来后再试 MSI（H4e-3） */
    MmioW32(E1000_REG_IMC, 0xFFFFFFFFu);
    MmioW32(E1000_REG_CTRL, MmioR32(E1000_REG_CTRL) | E1000_CTRL_RST);
    Spin = 100000;
    while (Spin-- > 0) {
        HalCpuRelax();
    }
    MmioW32(E1000_REG_IMC, 0xFFFFFFFFu);
    (void)MmioR32(E1000_REG_ICR);

    /* 环 + buffer：RX ring(1) + TX ring(1) + RX bufs(8页=16×2K) + TX buf(1) */
    Mem = (UINT8 *)PhysicalMemoryAllocatePages(1 + 1 + 8 + 1);
    if (!Mem) {
        return 0;
    }
    ZeroMemory(Mem, 11u * PAGE_SIZE);
    Phys = (UINT64)(UINTN)Mem;

    gRxRing = (E1000_RX_DESC *)(UINTN)Mem;
    gTxRing = (E1000_TX_DESC *)(UINTN)(Mem + PAGE_SIZE);
    gRxBufs = Mem + 2u * PAGE_SIZE;
    gTxBuf = Mem + 10u * PAGE_SIZE;

    for (i = 0; i < E1000_RING_COUNT; i++) {
        gRxRing[i].Addr = Phys + 2u * PAGE_SIZE + (UINT64)i * E1000_BUF_SIZE;
        gRxRing[i].Status = 0;
        gTxRing[i].Addr = 0;
        gTxRing[i].Status = E1000_TX_DD; /* 空闲 */
    }

    /* RX ring */
    MmioW32(E1000_REG_RDBAL, (UINT32)Phys);
    MmioW32(E1000_REG_RDBAH, (UINT32)(Phys >> 32));
    MmioW32(E1000_REG_RDLEN, E1000_RING_COUNT * 16u);
    MmioW32(E1000_REG_RDH, 0);
    gRxTail = E1000_RING_COUNT - 1u;
    MmioW32(E1000_REG_RDT, gRxTail);

    /* TX ring */
    MmioW32(E1000_REG_TDBAL, (UINT32)(Phys + PAGE_SIZE));
    MmioW32(E1000_REG_TDBAH, (UINT32)((Phys + PAGE_SIZE) >> 32));
    MmioW32(E1000_REG_TDLEN, E1000_RING_COUNT * 16u);
    MmioW32(E1000_REG_TDH, 0);
    MmioW32(E1000_REG_TDT, 0);
    gTxTail = 0;

    for (i = 0; i < 128; i++) {
        MmioW32(E1000_REG_MTA + i * 4u, 0);
    }

    ReadMac();

    MmioW32(E1000_REG_CTRL, MmioR32(E1000_REG_CTRL) | E1000_CTRL_SLU);
    MmioW32(E1000_REG_TIPG, 0x0060200Au);
    MmioW32(E1000_REG_RCTL,
            E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_BSIZE_2048 |
            E1000_RCTL_SECRC | E1000_RCTL_LBM_NONE);
    MmioW32(E1000_REG_TCTL,
            E1000_TCTL_EN | E1000_TCTL_PSP |
            (0x10u << E1000_TCTL_CT_SHIFT) | (0x40u << E1000_TCTL_COLD_SHIFT));

    if (!WaitLinkUp()) {
        ToyLogNet("Boot: E1000 Link Timeout\n");
        DebugWrite("e1000: STATUS.LU timeout\n");
        /* soft-fail：不置 gReady，桌面仍起 */
        gBar = 0;
        gPciDid = 0;
        return 0;
    }

    gReady = 1;
    gE1000UseIrq = 0;
    if (TryEnableMsiRx()) {
        if (gPciDid == E1000_DID_I219_LM) {
            ToyLogNet("Boot: I219 IRQ=MSI\n");
        } else if (gPciDid == E1000_DID_82574L) {
            ToyLogNet("Boot: E1000E IRQ=MSI\n");
        } else {
            ToyLogNet("Boot: E1000 IRQ=MSI\n");
        }
    } else if (gPciDid == E1000_DID_I219_LM) {
        ToyLogNet("Boot: I219\n");
    } else if (gPciDid == E1000_DID_82574L) {
        ToyLogNet("Boot: E1000E\n");
    } else {
        ToyLogNet("Boot: E1000\n");
    }
    DebugWrite("e1000: did=");
    DebugHex32((UINT32)gPciDid);
    DebugWrite(" bar=");
    DebugHex32((UINT32)gBarPhys);
    DebugWrite(gE1000UseIrq ? " irq=msi\n" : " irq=poll\n");
    return 1;
}

int E1000SendFrame(const UINT8 *Frame, UINTN Len) {
    E1000_TX_DESC *D;
    UINTN Wire = Len;
    int Spin;

    if (!gReady || !Frame || Len < 14) {
        return -1;
    }
    if (Wire > E1000_BUF_SIZE) {
        return -1;
    }
    if (Wire < 60) {
        Wire = 60;
    }

    D = &gTxRing[gTxTail];
    Spin = 100000;
    while (!(D->Status & E1000_TX_DD) && Spin-- > 0) {
        HalCpuRelax();
    }
    if (!(D->Status & E1000_TX_DD)) {
        return -1;
    }

    ZeroMemory(gTxBuf, Wire);
    CopyMemory(gTxBuf, Frame, Len);
    D->Addr = (UINT64)(UINTN)gTxBuf;
    D->Length = (UINT16)Wire;
    D->Cso = 0;
    D->Cmd = (UINT8)(E1000_TX_CMD_EOP | E1000_TX_CMD_IFCS | E1000_TX_CMD_RS);
    D->Status = 0;
    D->Css = 0;
    D->Special = 0;
    Fence();
    gTxTail = (UINT16)((gTxTail + 1u) % E1000_RING_COUNT);
    MmioW32(E1000_REG_TDT, gTxTail);

    Spin = 100000;
    while (!(D->Status & E1000_TX_DD) && Spin-- > 0) {
        HalCpuRelax();
    }
    return (D->Status & E1000_TX_DD) ? 0 : -1;
}

void E1000Poll(void) {
    UINT16 Next;

    if (!gReady) {
        return;
    }
    (void)MmioR32(E1000_REG_ICR);

    for (;;) {
        Next = (UINT16)((gRxTail + 1u) % E1000_RING_COUNT);
        if (!(gRxRing[Next].Status & E1000_RX_DD)) {
            break;
        }
        if (gRxRing[Next].Status & E1000_RX_EOP) {
            UINT16 Len = gRxRing[Next].Length;
            UINT8 *Buf = gRxBufs + (UINTN)Next * E1000_BUF_SIZE;
            if (Len >= 14) {
                NetInputFrame(Buf, Len);
            }
        }
        gRxRing[Next].Status = 0;
        gRxTail = Next;
        Fence();
        MmioW32(E1000_REG_RDT, gRxTail);
    }
}

/* PR-H4e-3：MSI 入口；仍可被 NetPoll 备份调用同一 E1000Poll */
void E1000Irq(void) {
    gStatIrq++;
    E1000Poll();
}

int E1000IrqEnabled(void) {
    return (gReady && gE1000UseIrq) ? 1 : 0;
}
