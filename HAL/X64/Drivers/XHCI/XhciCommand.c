/*
 * XhciCommand.c — PR-H-xhci-core-split-3：Command / Recover / WaitCommand
 * PR-H-xhci-evt-excl-1：门铃与等待同属独占窗
 * PR-H-xhci-evt-excl-4：Recover 全程独占；sick 前再 Recover 清挂起 TRB
 */
#include "XHCI/XhciInternal.h"

/* 等待命令环完成事件（调用方须已 EnterExclusive，或由此函数自管） */
int WaitCommand(int Timeout) {
    int Own = 0;
    int Result = -1;

    if (!XhciEventIsExclusive()) {
        XhciEventEnterExclusive();
        Own = 1;
    }

    if (!HalCpuIsHypervisor()) {
        UINT64 T0 = ReadTsc();
        /* Force PR 后 EnableSlot 真机可 >300ms；过短会误超时→CA→HID 伤 */
        UINT64 Need = 800ULL * 3000000ULL; /* ~800ms */
        UINT64 Mid = Need / 2;
        (void)Timeout;
        while (ReadTsc() - T0 < Need) {
            ProcessEventsRealPc();
            ServiceHidCompletions();
            if (gCmdDone) {
                Result = (gCmdCode == CC_SUCCESS) ? 0 : -1;
                break;
            }
            if ((ReadTsc() - T0) >= Mid) {
                Mid = Need + 1; /* 只刷一次 */
                ToyBootMarkUsb("boot: xhci cmd wait2\n");
            }
            __asm__ volatile ("pause");
        }
    } else {
        while (Timeout--) {
            ProcessEvents();
            ServiceHidCompletions();
            if (gCmdDone) {
                Result = (gCmdCode == CC_SUCCESS) ? 0 : -1;
                break;
            }
        }
    }

    if (Own) {
        XhciEventLeaveExclusive();
    }
    return Result;
}

/*
 * 命令超时恢复：CA 中止命令环，排空事件，再同步 enqueue。
 * excl-4：CA→排空→重建 全程独占（含 WaitClearMs，避免 StallMs 抢环）。
 * 私有环可 InitRing；固件环只按 CRCR dequeue 重解析，勿盲目清环/切软环。
 */
void RecoverCommandRing(void) {
    UINT64 Cr;
    UINT64 Ptr;
    UINT32 Rcs;
    int i;
    XHCI_TRB *Base;
    UINT32 Size;
    UINT32 Enq;
    UINT32 Pcs;
    int Own = 0;

    ToyBootMarkUsb("boot: xhci cmd recover\n");
    BootLog("boot: xhci command timeout, recovering...\n");

    if (!XhciEventIsExclusive()) {
        XhciEventEnterExclusive();
        Own = 1;
    }

    Cr = ReadMmio64(gOperationalBase + 0x18);
    Ptr = Cr & ~0x3FULL;
    Rcs = (UINT32)(Cr & 1u);
    if (Ptr != 0) {
        WriteMmio64(gOperationalBase + 0x18, Ptr | (UINT64)(Rcs & 1u) | CRCR_CA);
        Fence();
        if (!HalCpuIsHypervisor()) {
            (void)WaitClearMs(gOperationalBase + 0x18, CRCR_CRR, 200);
        } else {
            (void)WaitClear(gOperationalBase + 0x18, CRCR_CRR, 100000);
        }
    }

    for (i = 0; i < 64; i++) {
        ProcessEvents();
        ServiceHidCompletions();
    }

    Cr = ReadMmio64(gOperationalBase + 0x18);
    Ptr = Cr & ~0x3FULL;
    Rcs = (UINT32)(Cr & 1u);

    if (gCmdRingLive == gCmdRing) {
        InitRing(gCmdRing, &gCmd, RING_SIZE);
        FlushDma(gCmdRing, RING_SIZE * sizeof(XHCI_TRB));
        WriteMmio64(gOperationalBase + 0x18, PointerToPhysical(gCmdRing) | 1ULL);
        Fence();
    } else if (Ptr != 0) {
        Base = 0;
        Size = 0;
        Enq = 0;
        Pcs = 0;
        if (ResolveFwCmdRing(Ptr, Rcs, &Base, &Size, &Enq, &Pcs) == 0) {
            gCmdRingLive = Base;
            gCmd.Enq = Enq;
            gCmd.Pcs = Pcs;
            gCmd.Size = Size;
        } else {
            /* 解析失败：跟 dequeue 对齐，勿切私有环再写回固件 Ptr（会踩 CRCR） */
            gCmdRingLive = (XHCI_TRB *)(UINTN)Ptr;
            gCmd.Enq = 0;
            gCmd.Pcs = Rcs & 1u;
        }
        WriteMmio64(gOperationalBase + 0x18, Ptr | (UINT64)(Rcs & 1u));
        Fence();
    }

    gCmdDone = 0;
    gCmdCode = 0;
    /* 勿把未完成的命令完成误当成下一笔 */
    ToyBootMarkUsb("boot: xhci cmd ring recovered\n");

    if (Own) {
        XhciEventLeaveExclusive();
    }
}

/* 提交一条命令 TRB 并等待完成；超时则 CA 恢复并重试一次 */
int Command(UINT64 Param, UINT32 Control, UINT32 *SlotOut) {
    int Wait = HalCpuIsHypervisor() ? 150000 : 200000;
    int RealPc = !HalCpuIsHypervisor();
    int Attempt;
    const char *Name = CmdTrbName(Control);

    if (gXhciCmdSick) {
        return -1;
    }

    for (Attempt = 0; Attempt < 2; Attempt++) {
        gCmdDone = 0;
        gCmdCode = 0;
        gCmdSlot = 0;
        /* 门铃与 Wait 同窗：消灭门铃→置旗空隙 */
        XhciEventEnterExclusive();
        Enqueue(gCmdRingLive, &gCmd, Param, 0, Control | TRB_IOC);
        RingDoorbell(0, 0);
        Fence();
        if (WaitCommand(Wait) >= 0) {
            XhciEventLeaveExclusive();
            if (SlotOut) {
                *SlotOut = gCmdSlot;
            }
            DiagChk(Name, 1, "cc=1", gCmdCode, 2);
            if (SlotOut && ((Control >> 10) & 0x3F) == TRB_ENABLE_SLOT) {
                DiagChk("EnableSlot.slot", *SlotOut != 0 && *SlotOut <= gDcbaaMaxSlot,
                        "slot=1..N", *SlotOut, 2);
            }
            gXhciCmdSick = 0;
            return 0;
        }
        XhciEventLeaveExclusive();

        if (gCmdDone) {
            DiagChk(Name, 0, "cc=1", gCmdCode, 2);
            return -1;
        }

        DiagChk(Name, 0, "event+cc=1", RealPc ? ReadMmio32(gOperationalBase + 4) : 0, 8);
        RecoverCommandRing();
        /* CA 后重武装键鼠中断，避免 Recover 吞掉完成却未再投递 */
        if (gSlotId != 0 && gIntrDci != 0) {
            QueueIntr();
        }
        if (gMouseSlotId != 0 && gMouseIntrDci != 0) {
            QueueMouseIntr();
        }
        if (Attempt == 0) {
            BootLog("xhci retry after cmd recover\n");
        }
    }
    DiagChkStr(Name, 0, "ok after retry", "fail");
    /*
     * excl-4：第二次仍超时 → 再 Recover 一次，勿把挂起命令 TRB 留在环上
     *（曾致 irq-stall / 鼠假死），然后再标 sick。
     */
    RecoverCommandRing();
    if (gSlotId != 0 && gIntrDci != 0) {
        QueueIntr();
    }
    if (gMouseSlotId != 0 && gMouseIntrDci != 0) {
        QueueMouseIntr();
    }
    gXhciCmdSick = 1;
    BootLog("boot: xhci cmd sick (timeout)\n");
    return -1;
}
