/*
 * EhciSched.c — DMA（UC）+ reclaim 头 + CtrlQH + ASE（PR-H-ehci-2 · 2h）
 *
 * 假说：单 H-bit 工作 QH + 32B qTD 在 Intel 64-bit 寻址下 HC 不执行（tok Active+Cerr=3）。
 */
#include "EhciPrivate.h"
#include "PhysicalMemory.h"
#include "VirtualMemory.h"
#include "ToySerialLog.h"
#include "Hal.h"

#ifndef PTE_PWT
#define PTE_PWT HAL_PAGE_PWT
#define PTE_PCD HAL_PAGE_PCD
#endif

#define EHCI_DMA_PAGES 3u

static void Zero(UINT8 *P, UINT32 N) {
    UINT32 i;
    for (i = 0; i < N; i++) {
        P[i] = 0;
    }
}

static void ZeroQh(EHCI_QH *Q) {
    UINT32 i;
    UINT8 *P = (UINT8 *)Q;
    for (i = 0; i < sizeof(*Q); i++) {
        P[i] = 0;
    }
}

int EhciSchedStart(EHCI_CTRL *C) {
    UINT32 i;
    UINT32 Cmd;
    UINT32 Hcc;
    UINT64 Phys;
    UINT8 *Mem;
    int Spin;

    if (!C || !C->Up || C->Sched) {
        return C && C->Sched ? 1 : 0;
    }

    Mem = (UINT8 *)PhysicalMemoryAllocatePages(EHCI_DMA_PAGES);
    if (!Mem) {
        ToyBootMarkUsb("Boot: EHCI DMA fail\n");
        return 0;
    }
    Phys = (UINT64)(UINTN)Mem;
    if (Phys > 0xFFFFFFFFu ||
        Phys + EHCI_DMA_PAGES * PAGE_SIZE > 0x100000000ull) {
        ToyBootMarkUsb("Boot: EHCI DMA >4G\n");
        return 0;
    }
    Zero(Mem, EHCI_DMA_PAGES * PAGE_SIZE);
    /* WB→UC：避免 qTD Active 粘在 cache */
    if (VirtualMemoryMapRange(Phys, Phys, EHCI_DMA_PAGES * PAGE_SIZE,
                              PTE_PRESENT | PTE_WRITABLE | PTE_PWT | PTE_PCD) !=
        0) {
        ToyBootMarkUsb("Boot: EHCI DMA UC map fail\n");
        return 0;
    }
    C->Dma = Mem;
    C->DmaPhys = Phys;

    /*
     * page0: 帧表
     * page1+: AsyncHead(128) CtrlQh(128) IntrQh(128) | qTD×5(64) | bufs
     */
    C->FrameList = (UINT32 *)(UINTN)Mem;
    C->AsyncHead = (EHCI_QH *)(UINTN)(Mem + PAGE_SIZE);
    C->CtrlQh = (EHCI_QH *)(UINTN)(Mem + PAGE_SIZE + 128);
    C->IntrQh = (EHCI_QH *)(UINTN)(Mem + PAGE_SIZE + 256);
    C->Qtds = (EHCI_QTD *)(UINTN)(Mem + PAGE_SIZE + 512);
    C->SetupBuf = Mem + PAGE_SIZE + 512 + 5 * 64;
    C->CtrlBuf = C->SetupBuf + 64;
    C->ReportBuf = C->CtrlBuf + 512;
    C->BulkBuf = C->ReportBuf + 64; /* 512B BOT bounce */

    ZeroQh(C->AsyncHead);
    ZeroQh(C->CtrlQh);
    ZeroQh(C->IntrQh);

    /* reclaim 头：H=1，overlay Halted，永不执行传输 */
    C->AsyncHead->Horiz =
        (UINT32)EhciPtrPhys(C->CtrlQh) | EHCI_LINK_TYPE_QH;
    C->AsyncHead->EpChar = (1u << 15) | (8u << 16) | (2u << 12);
    C->AsyncHead->EpCap = 1u << 30;
    C->AsyncHead->Next = EHCI_LINK_TERMINATE;
    C->AsyncHead->AltNext = EHCI_LINK_TERMINATE;
    C->AsyncHead->Token = EHCI_QTD_HALTED;

    /* 工作 CtrlQH：无 H；HS；DTC；RL（C 位仅 FS/LS） */
    C->CtrlQh->Horiz =
        (UINT32)EhciPtrPhys(C->AsyncHead) | EHCI_LINK_TYPE_QH;
    C->CtrlQh->EpChar = (64u << 16) | (2u << 12) | (1u << 14) | (0xFu << 28);
    C->CtrlQh->EpCap = 1u << 30;
    C->CtrlQh->Next = EHCI_LINK_TERMINATE;
    C->CtrlQh->AltNext = EHCI_LINK_TERMINATE;
    C->CtrlQh->Token = 0;

    C->IntrQh->Horiz = EHCI_LINK_TERMINATE;
    C->IntrQh->EpChar = (8u << 16) | (2u << 12);
    C->IntrQh->EpCap = (1u << 30) | 0xFFu;
    C->IntrQh->Next = EHCI_LINK_TERMINATE;
    C->IntrQh->AltNext = EHCI_LINK_TERMINATE;
    C->IntrQh->Token = 0;

    for (i = 0; i < EHCI_FRAME_ENTRIES; i++) {
        C->FrameList[i] =
            (UINT32)EhciPtrPhys(C->IntrQh) | EHCI_LINK_TYPE_QH;
    }

    Hcc = EhciR32(C->Cap, EHCI_HCCPARAMS);
    EhciFence();
    EhciW32(C->Op, EHCI_CTRLDSSEGMENT, 0);
    EhciW32(C->Op, EHCI_PERIODICLISTBASE, (UINT32)Phys);
    /* ASE=0 时写 ASYNCLISTADDR → reclaim 头 */
    EhciW32(C->Op, EHCI_ASYNCLISTADDR, (UINT32)EhciPtrPhys(C->AsyncHead));
    EhciW32(C->Op, EHCI_FRINDEX, 0);

    if (EHCI_HCC_64ADDR(Hcc)) {
        ToyBootMarkUsb("Boot: EHCI HCC 64-addr\n");
    }

    Cmd = EhciR32(C->Op, EHCI_USBCMD);
    Cmd &= ~(EHCI_CMD_PSE | EHCI_CMD_ASE);
    /* 关 Async Park，FS split 更稳 */
    Cmd &= ~((1u << 11) | (3u << 8));
    Cmd |= EHCI_CMD_RS | EHCI_CMD_ASE;
    EhciW32(C->Op, EHCI_USBCMD, Cmd);

    Spin = 400000;
    while (Spin-- > 0) {
        UINT32 St = EhciR32(C->Op, EHCI_USBSTS);
        if ((St & EHCI_STS_ASS) && (St & EHCI_STS_HCHALTED) == 0) {
            break;
        }
        HalCpuRelax();
    }
    if (Spin <= 0) {
        ToyBootMarkUsb("Boot: EHCI ASE timeout\n");
        return 0;
    }
    C->Sched = 1;
    return 1;
}

void EhciSchedEnablePeriodic(EHCI_CTRL *C) {
    UINT32 Cmd;
    int Spin;

    if (!C || !C->Sched) {
        return;
    }
    Cmd = EhciR32(C->Op, EHCI_USBCMD);
    if (Cmd & EHCI_CMD_PSE) {
        return;
    }
    EhciW32(C->Op, EHCI_USBCMD, Cmd | EHCI_CMD_PSE);
    Spin = 200000;
    while (Spin-- > 0) {
        if (EhciR32(C->Op, EHCI_USBSTS) & EHCI_STS_PSS) {
            break;
        }
        HalCpuRelax();
    }
}
