/*
 * E1000Setup.c — e1000 BAR / 环 / 链路 Setup（PR-S3-e1000-2）
 */
#include "E1000.h"
#include "E1000Private.h"
#include "PhysicalMemory.h"
#include "VirtualMemory.h"
#include "Debug.h"
#include "Hal.h"
#include "ToySerialLog.h"

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
    int LinkOk;

    if (gE1000Ready) {
        return 1;
    }
    /* 刀 #84 副作用：HalIwlClaim 后再 Probe NET，链路失败时曾清空 BAR → 二次 Flush */
    if (gE1000SetupOnce) {
        return 0;
    }
    gE1000SetupOnce = 1;
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

    gE1000RxRing = (E1000_RX_DESC *)(UINTN)Mem;
    gE1000TxRing = (E1000_TX_DESC *)(UINTN)(Mem + PAGE_SIZE);
    gE1000RxBufs = Mem + 2u * PAGE_SIZE;
    gE1000TxBuf = Mem + 10u * PAGE_SIZE;

    for (i = 0; i < E1000_RING_COUNT; i++) {
        gE1000RxRing[i].Addr = Phys + 2u * PAGE_SIZE + (UINT64)i * E1000_BUF_SIZE;
        gE1000RxRing[i].Status = 0;
        gE1000TxRing[i].Addr = 0;
        gE1000TxRing[i].Status = E1000_TX_DD; /* 空闲 */
    }

    /* RX ring */
    MmioW32(E1000_REG_RDBAL, (UINT32)Phys);
    MmioW32(E1000_REG_RDBAH, (UINT32)(Phys >> 32));
    MmioW32(E1000_REG_RDLEN, E1000_RING_COUNT * 16u);
    MmioW32(E1000_REG_RDH, 0);
    gE1000RxTail = E1000_RING_COUNT - 1u;
    MmioW32(E1000_REG_RDT, gE1000RxTail);

    /* TX ring */
    MmioW32(E1000_REG_TDBAL, (UINT32)(Phys + PAGE_SIZE));
    MmioW32(E1000_REG_TDBAH, (UINT32)((Phys + PAGE_SIZE) >> 32));
    MmioW32(E1000_REG_TDLEN, E1000_RING_COUNT * 16u);
    MmioW32(E1000_REG_TDH, 0);
    MmioW32(E1000_REG_TDT, 0);
    gE1000TxTail = 0;

    /*
     * PR-N-i219-tx10：唯一 Flush OK 过的位置 = 环写完立刻冲。
     * tx7/9 在 ReadMac/SLU/TIPG 之后冲 → 均为 NoDD。
     */
    E1000FlushI219Rings();

    for (i = 0; i < 128; i++) {
        MmioW32(E1000_REG_MTA + i * 4u, 0);
    }

    ReadMac();

    MmioW32(E1000_REG_CTRL, MmioR32(E1000_REG_CTRL) | E1000_CTRL_SLU);
    MmioW32(E1000_REG_TIPG, 0x0060200Au);
    /*
     * PR-N-i219-tx11：Flush OK 后勿写满配 TCTL（0x4010A）。
     * 满配曾导致事后冲环 NoDD；保留 flush 留下的 EN + TXDCTL。
     */
    if (gPciDid != E1000_DID_I219_LM) {
        MmioW32(E1000_REG_TCTL,
                E1000_TCTL_EN | E1000_TCTL_PSP |
                (0x10u << E1000_TCTL_CT_SHIFT) |
                (0x40u << E1000_TCTL_COLD_SHIFT));
    }
    MmioW32(E1000_REG_RCTL,
            E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_BSIZE_2048 |
            E1000_RCTL_SECRC | E1000_RCTL_LBM_NONE);

    LinkOk = WaitLinkUp();
    if (!LinkOk) {
        ToyLogNet("Boot: E1000 Link Timeout\n");
        DebugWrite("e1000: STATUS.LU timeout (soft-up for static IP)\n");
        /* 仍 gE1000Ready：NUC 静态 IP / 稍后插线；勿清 BAR，避免二次 Probe 再 Flush */
    }

    gE1000Ready = 1;
    gE1000UseIrq = 0;
    /*
     * PR-N-i219-tx8：I219 保持 poll（tx7 已是 Flush NoDD，MSI 非主因；
     * 仍禁用以免多变量）。QEMU e1000e 照旧 MSI。
     */
    if (gPciDid != E1000_DID_I219_LM && TryEnableMsiRx()) {
        if (gPciDid == E1000_DID_82574L) {
            ToyLogNet("Boot: E1000E IRQ=MSI\n");
        } else {
            ToyLogNet("Boot: E1000 IRQ=MSI\n");
        }
    } else if (gPciDid == E1000_DID_I219_LM) {
        ToyLogNet("Boot: I219 IRQ=Poll\n");
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
