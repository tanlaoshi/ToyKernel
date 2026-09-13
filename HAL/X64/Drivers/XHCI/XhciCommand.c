/*
 * XhciCommand.c — PR-H-xhci-core-split-3：Command / Recover / WaitCommand
 *
 * 从 XhciCore.c 原样搬家；不改语义。命令环全局仍在 XhciCore.c（勿迁 BSS）。
 * WaitCommand 期间仍设 gXhciCmdWaiting，Irq/Drain 勿并发 ProcessEvents。
 */
#include "XHCI/XhciInternal.h"

/* 等待命令环完成事件 */
int WaitCommand(int Timeout) {
    if (!HalCpuIsHypervisor()) {
        UINT64 T0 = ReadTsc();
        /* Force PR 后 EnableSlot 真机可 >300ms；过短会误超时→CA→HID 伤 */
        UINT64 Need = 800ULL * 3000000ULL; /* ~800ms */
        UINT64 Mid = Need / 2;
        (void)Timeout;
        gXhciCmdWaiting = 1;
        while (ReadTsc() - T0 < Need) {
            ProcessEventsRealPc();
            ServiceHidCompletions();
            if (gCmdDone) {
                gXhciCmdWaiting = 0;
                return (gCmdCode == CC_SUCCESS) ? 0 : -1;
            }
            if ((ReadTsc() - T0) >= Mid) {
                Mid = Need + 1; /* 只刷一次 */
                ToyBootMarkUsb("boot: xhci cmd wait2\n");
            }
            __asm__ volatile ("pause");
        }
        gXhciCmdWaiting = 0;
        return -1;
    }
    while (Timeout--) {
        ProcessEvents();
        ServiceHidCompletions();
        if (gCmdDone) {
            return (gCmdCode == CC_SUCCESS) ? 0 : -1;
        }
    }
    return -1;
}

/*
 * 命令超时恢复：CA 中止命令环，排空事件，再同步 enqueue。
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

    ToyBootMarkUsb("boot: xhci cmd recover\n");
    BootLog("boot: xhci command timeout, recovering...\n");

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
    ToyBootMarkUsb("boot: xhci cmd ring recovered\n");
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
        Enqueue(gCmdRingLive, &gCmd, Param, 0, Control | TRB_IOC);
        RingDoorbell(0, 0);
        Fence();
        if (WaitCommand(Wait) >= 0) {
            if (SlotOut) {
                *SlotOut = gCmdSlot;
            }
            /* want cc=1(Success)；got=完成码；EnableSlot 另看 slot */
            DiagChk(Name, 1, "cc=1", gCmdCode, 2);
            if (SlotOut && ((Control >> 10) & 0x3F) == TRB_ENABLE_SLOT) {
                DiagChk("EnableSlot.slot", *SlotOut != 0 && *SlotOut <= gDcbaaMaxSlot,
                        "slot=1..N", *SlotOut, 2);
            }
            gXhciCmdSick = 0;
            return 0;
        }

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
    gXhciCmdSick = 1;
    BootLog("boot: xhci cmd sick (timeout)\n");
    return -1;
}
