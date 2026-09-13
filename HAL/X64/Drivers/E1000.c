/*
 * E1000.c — Intel e1000 / e1000e 轮询驱动（PR-H4 + PR-H4e-1）
 *
 * QEMU: -device e1000 | -device e1000e
 * 同步 TX + RX ring；无中断 / 无 MSI。无卡或无链路时 Setup 失败，不挡桌面。
 */
#include "E1000.h"
#include "PCIe.h"
#include "PhysicalMemory.h"
#include "VirtualMemory.h"
#include "Debug.h"
#include "Hal.h"
#include "HalSerial.h"
#include "Net.h"
#include "ToySerialLog.h"

#define E1000_VENDOR          0x8086u
#define E1000_DID_82540EM     0x100Eu /* QEMU -device e1000 */
#define E1000_DID_82574L      0x10D3u /* QEMU -device e1000e */

#define E1000_REG_CTRL        0x0000u
#define E1000_REG_STATUS      0x0008u
#define E1000_REG_EEC         0x0010u
#define E1000_REG_EERD        0x0014u
#define E1000_REG_ICR         0x00C0u
#define E1000_REG_IMS         0x00D0u
#define E1000_REG_IMC         0x00D8u
#define E1000_REG_RCTL        0x0100u
#define E1000_REG_TCTL        0x0400u
#define E1000_REG_TIPG        0x0410u
#define E1000_REG_RDBAL       0x2800u
#define E1000_REG_RDBAH       0x2804u
#define E1000_REG_RDLEN       0x2808u
#define E1000_REG_RDH         0x2810u
#define E1000_REG_RDT         0x2818u
#define E1000_REG_TDBAL       0x3800u
#define E1000_REG_TDBAH       0x3804u
#define E1000_REG_TDLEN       0x3808u
#define E1000_REG_TDH         0x3810u
#define E1000_REG_TDT         0x3818u
#define E1000_REG_MTA         0x5200u
#define E1000_REG_RAL         0x5400u
#define E1000_REG_RAH         0x5404u

#define E1000_CTRL_SLU        (1u << 6)
#define E1000_CTRL_RST        (1u << 26)

#define E1000_STATUS_FD       (1u << 0)  /* Full Duplex */
#define E1000_STATUS_LU       (1u << 1)
#define E1000_STATUS_SPEED_SHIFT 6
#define E1000_STATUS_SPEED_MASK  (3u << E1000_STATUS_SPEED_SHIFT)

#define E1000_EERD_START      (1u << 0)
#define E1000_EERD_DONE       (1u << 1)
#define E1000_EERD_ADDR_SHIFT 2
#define E1000_EERD_DATA_SHIFT 16
/* 82540 旧式：DONE=bit4，ADDR=bits15:8 */
#define E1000_EERD_DONE_LEGACY (1u << 4)
#define E1000_EERD_ADDR_LEGACY_SHIFT 8

#define E1000_RCTL_EN         (1u << 1)
#define E1000_RCTL_SBP        (1u << 2)
#define E1000_RCTL_UPE        (1u << 3)
#define E1000_RCTL_MPE        (1u << 4)
#define E1000_RCTL_LPE        (1u << 5)
#define E1000_RCTL_LBM_NONE   (0u << 6)
#define E1000_RCTL_BAM        (1u << 15)
#define E1000_RCTL_BSIZE_2048 (0u << 16)
#define E1000_RCTL_SECRC      (1u << 26)

#define E1000_TCTL_EN         (1u << 1)
#define E1000_TCTL_PSP        (1u << 3)
#define E1000_TCTL_CT_SHIFT   4
#define E1000_TCTL_COLD_SHIFT 12

#define E1000_RX_DD           (1u << 0)
#define E1000_RX_EOP          (1u << 1)
#define E1000_TX_DD           (1u << 0)
#define E1000_TX_CMD_EOP      (1u << 0)
#define E1000_TX_CMD_IFCS     (1u << 1)
#define E1000_TX_CMD_RS       (1u << 3)

#define E1000_RING_COUNT      16u
#define E1000_BUF_SIZE        2048u

typedef struct {
    UINT64 Addr;
    UINT16 Length;
    UINT16 Checksum;
    UINT8  Status;
    UINT8  Errors;
    UINT16 Special;
} __attribute__((packed)) E1000_RX_DESC;

typedef struct {
    UINT64 Addr;
    UINT16 Length;
    UINT8  Cso;
    UINT8  Cmd;
    UINT8  Status;
    UINT8  Css;
    UINT16 Special;
} __attribute__((packed)) E1000_TX_DESC;

static const UINT16 gE1000Ids[] = {
    E1000_DID_82540EM, /* 82540EM — QEMU e1000 */
    0x100F,            /* 82545EM */
    E1000_DID_82574L,  /* 82574L — e1000e */
    0x10F5,            /* 82567LM */
    0
};

static volatile UINT8 *gBar;
static UINT64 gBarPhys;
static UINT16 gPciDid;
static E1000_RX_DESC *gRxRing;
static E1000_TX_DESC *gTxRing;
static UINT8 *gRxBufs;
static UINT8 *gTxBuf;
static UINT16 gRxTail;
static UINT16 gTxTail;
static UINT8 gMac[6];
static int gReady;

static UINT32 MmioR32(UINT32 Off) {
    return *(volatile UINT32 *)(gBar + Off);
}

static void MmioW32(UINT32 Off, UINT32 Val) {
    *(volatile UINT32 *)(gBar + Off) = Val;
}

static void Fence(void) {
    __asm__ volatile("mfence" ::: "memory");
}

static void ZeroMemory(void *Ptr, UINTN Len) {
    UINT8 *B = (UINT8 *)Ptr;
    UINTN i;
    for (i = 0; i < Len; i++) {
        B[i] = 0;
    }
}

static void CopyMemory(void *Dst, const void *Src, UINTN Len) {
    UINT8 *D = (UINT8 *)Dst;
    const UINT8 *S = (const UINT8 *)Src;
    UINTN i;
    for (i = 0; i < Len; i++) {
        D[i] = S[i];
    }
}

static int PciFindE1000(UINT8 *Bus, UINT8 *Dev, UINT8 *Fn, UINT64 *BarOut,
                        UINT16 *DidOut) {
    int B;
    int D;
    int F;
    int I;

    for (B = 0; B < 256; B++) {
        for (D = 0; D < 32; D++) {
            for (F = 0; F < 8; F++) {
                UINT32 VidDid = PciReadConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x00);
                UINT16 Vid = (UINT16)(VidDid & 0xFFFF);
                UINT16 Did = (UINT16)(VidDid >> 16);
                UINT32 Lo;
                UINT32 Hi;
                UINT64 Bar;
                UINT32 Cmd;

                if (Vid != E1000_VENDOR) {
                    continue;
                }
                for (I = 0; gE1000Ids[I] != 0; I++) {
                    if (Did == gE1000Ids[I]) {
                        break;
                    }
                }
                if (gE1000Ids[I] == 0) {
                    continue;
                }

                Cmd = PciReadConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x04);
                PciWriteConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x04, Cmd | 0x06);

                Lo = PciReadConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x10);
                if (Lo & 1u) {
                    continue;
                }
                Bar = Lo & 0xFFFFFFF0ULL;
                if (((Lo >> 1) & 3u) == 2u) {
                    Hi = PciReadConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x14);
                    Bar |= ((UINT64)Hi) << 32;
                }
                if (Bar == 0) {
                    continue;
                }
                *Bus = (UINT8)B;
                *Dev = (UINT8)D;
                *Fn = (UINT8)F;
                *BarOut = Bar;
                if (DidOut) {
                    *DidOut = Did;
                }
                return 1;
            }
        }
    }
    return 0;
}

/* 82571+/82574 EERD：START + ADDR<<2，DONE=bit1，DATA=bits31:16 */
static int EepromReadWordE1000e(UINT16 Addr, UINT16 *Out) {
    UINT32 Val;
    int Spin;

    MmioW32(E1000_REG_EERD,
            E1000_EERD_START | ((UINT32)Addr << E1000_EERD_ADDR_SHIFT));
    Spin = 100000;
    while (Spin-- > 0) {
        Val = MmioR32(E1000_REG_EERD);
        if (Val & E1000_EERD_DONE) {
            *Out = (UINT16)(Val >> E1000_EERD_DATA_SHIFT);
            return 1;
        }
        HalCpuRelax();
    }
    return 0;
}

/* 82540 旧 EERD：ADDR<<8，DONE=bit4 */
static int EepromReadWordLegacy(UINT16 Addr, UINT16 *Out) {
    UINT32 Val;
    int Spin;

    MmioW32(E1000_REG_EERD,
            E1000_EERD_START | ((UINT32)Addr << E1000_EERD_ADDR_LEGACY_SHIFT));
    Spin = 100000;
    while (Spin-- > 0) {
        Val = MmioR32(E1000_REG_EERD);
        if (Val & E1000_EERD_DONE_LEGACY) {
            *Out = (UINT16)(Val >> E1000_EERD_DATA_SHIFT);
            return 1;
        }
        HalCpuRelax();
    }
    return 0;
}

static int EepromReadWord(UINT16 Addr, UINT16 *Out) {
    if (gPciDid == E1000_DID_82574L || gPciDid == 0x10F5) {
        if (EepromReadWordE1000e(Addr, Out)) {
            return 1;
        }
    }
    if (EepromReadWordLegacy(Addr, Out)) {
        return 1;
    }
    if (gPciDid != E1000_DID_82574L && gPciDid != 0x10F5) {
        return EepromReadWordE1000e(Addr, Out);
    }
    return 0;
}

static int MacFromEeprom(void) {
    UINT16 W0;
    UINT16 W1;
    UINT16 W2;

    if (!EepromReadWord(0, &W0) || !EepromReadWord(1, &W1) ||
        !EepromReadWord(2, &W2)) {
        return 0;
    }
    if ((W0 | W1 | W2) == 0 || (W0 == 0xFFFF && W1 == 0xFFFF && W2 == 0xFFFF)) {
        return 0;
    }
    gMac[0] = (UINT8)(W0 & 0xFF);
    gMac[1] = (UINT8)(W0 >> 8);
    gMac[2] = (UINT8)(W1 & 0xFF);
    gMac[3] = (UINT8)(W1 >> 8);
    gMac[4] = (UINT8)(W2 & 0xFF);
    gMac[5] = (UINT8)(W2 >> 8);
    MmioW32(E1000_REG_RAL,
            (UINT32)gMac[0] | ((UINT32)gMac[1] << 8) |
            ((UINT32)gMac[2] << 16) | ((UINT32)gMac[3] << 24));
    MmioW32(E1000_REG_RAH,
            (UINT32)gMac[4] | ((UINT32)gMac[5] << 8) | (1u << 31));
    return 1;
}

static void ProgramClassroomMac(void) {
    gMac[0] = 0x52;
    gMac[1] = 0x54;
    gMac[2] = 0x00;
    gMac[3] = 0x12;
    gMac[4] = 0x34;
    gMac[5] = 0x56;
    MmioW32(E1000_REG_RAL,
            (UINT32)gMac[0] | ((UINT32)gMac[1] << 8) |
            ((UINT32)gMac[2] << 16) | ((UINT32)gMac[3] << 24));
    MmioW32(E1000_REG_RAH,
            (UINT32)gMac[4] | ((UINT32)gMac[5] << 8) | (1u << 31));
}

static void ReadMac(void) {
    UINT32 Ral = MmioR32(E1000_REG_RAL);
    UINT32 Rah = MmioR32(E1000_REG_RAH);

    gMac[0] = (UINT8)(Ral & 0xFF);
    gMac[1] = (UINT8)((Ral >> 8) & 0xFF);
    gMac[2] = (UINT8)((Ral >> 16) & 0xFF);
    gMac[3] = (UINT8)((Ral >> 24) & 0xFF);
    gMac[4] = (UINT8)(Rah & 0xFF);
    gMac[5] = (UINT8)((Rah >> 8) & 0xFF);
    if ((Ral | (Rah & 0xFFFFu)) != 0) {
        return;
    }
    /* RAL 空：试 NVM（82574 常见），再课堂假 MAC */
    (void)MmioR32(E1000_REG_EEC);
    if (MacFromEeprom()) {
        DebugWrite("e1000: mac from nvm\n");
        return;
    }
    ProgramClassroomMac();
    DebugWrite("e1000: mac classroom fallback\n");
}

/* 等 STATUS.LU；超时返回 0（Setup soft-fail，不挡桌面） */
static int WaitLinkUp(void) {
    int Spin = 2000000;

    while (Spin-- > 0) {
        if (MmioR32(E1000_REG_STATUS) & E1000_STATUS_LU) {
            return 1;
        }
        HalCpuRelax();
    }
    return 0;
}

int E1000Ready(void) {
    return gReady;
}

void E1000GetMac(UINT8 Mac[6]) {
    CopyMemory(Mac, gMac, 6);
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

/* 82574 → "e1000e"；其它 → "e1000" */
const char *E1000ChipName(void) {
    if (gPciDid == E1000_DID_82574L) {
        return "e1000e";
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

    if (VirtualMemoryMapRange(Bar, Bar, 0x20000, PTE_PRESENT | PTE_WRITABLE) != 0) {
        DebugWrite("e1000: map BAR failed\n");
        return 0;
    }
    gBarPhys = Bar;
    gBar = (volatile UINT8 *)(UINTN)Bar;

    /* 复位；保持 IMC 全掩（本刀无 MSI） */
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
        ToyLogNet("boot: e1000 link timeout\n");
        DebugWrite("e1000: STATUS.LU timeout\n");
        /* soft-fail：不置 gReady，桌面仍起 */
        gBar = 0;
        gPciDid = 0;
        return 0;
    }

    gReady = 1;
    if (gPciDid == E1000_DID_82574L) {
        ToyLogNet("boot: e1000e\n");
    } else {
        ToyLogNet("boot: e1000\n");
    }
    DebugWrite("e1000: did=");
    DebugHex32((UINT32)gPciDid);
    DebugWrite(" bar=");
    DebugHex32((UINT32)gBarPhys);
    DebugWrite("\n");
    (void)Bus;
    (void)Dev;
    (void)Fn;
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
