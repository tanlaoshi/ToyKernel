/*
 * XhciEvent.c — PR-H-xhci-msc-split-2：事件环 ProcessEvents*
 * PR-H-xhci-evt-excl-1：独占窗 API（门铃/Wait 同消费者）
 * PR-H-xhci-evt-excl-2：gEvtConsumerLock + ProcessEventsLocked
 * PR-F-xhci-3：Transfer 分发见 XhciEventXfer.c
 *
 * DMA/环全局仍在 Xhci.c（勿迁 BSS）。
 */
#include "XHCI/XhciInternal.h"

/* 嵌套深度：Command Enter + Wait* Enter 可叠一层 */
static volatile int gEvtExclusiveDepth;

void XhciEventEnterExclusive(void) {
    gEvtExclusiveDepth++;
    gXhciCmdWaiting = 1; /* Irq/Drain 旧门控仍认此旗 */
    Fence();
}

void XhciEventLeaveExclusive(void) {
    Fence();
    if (gEvtExclusiveDepth > 0) {
        gEvtExclusiveDepth--;
    }
    if (gEvtExclusiveDepth == 0) {
        gXhciCmdWaiting = 0;
    }
}

int XhciEventIsExclusive(void) {
    return gEvtExclusiveDepth > 0 ? 1 : 0;
}

/* 处理事件环中所有待处理 TRB（命令完成、传输完成）；调用方负责串行 */
void ProcessEventsLocked(void) {
    int Progress = 0;
    UINT32 EvtSize = gEvtRingSize ? gEvtRingSize : EVT_SIZE;
    int Guard = 0;

    for (;;) {
        XHCI_TRB *Evt;
        UINT32 Type;
        UINT32 Code;
        UINT32 Slot;

        if (++Guard > (int)(EvtSize * 2u + 8u)) {
            break; /* 固件残留事件勿死循环 */
        }
        Evt = &gEvtRingLive[gEvtDeq];
        /* 真机：先 invalidate，再读 Cycle，避免缓存挡住完成事件 */
        FlushDma(Evt, sizeof(*Evt));
        if ((Evt->Control & TRB_C) != gEvtCcs) {
            break;
        }
        Progress = 1;
        gStatEvtRing++;

        Type = TrbType(Evt->Control);
        Code = (Evt->Status >> 24) & 0xFF;
        Slot = (Evt->Control >> 24) & 0xFF;

        if (Type == TRB_CMD_COMPLETION) {
            gCmdCode = Code;
            gCmdSlot = Slot;
            gCmdDone = 1;
        } else if (Type == TRB_TRANSFER_EVENT) {
            XhciEventHandleTransfer(Evt);
        }

        gEvtDeq++;
        if (gEvtDeq == EvtSize) {
            gEvtDeq = 0;
            gEvtCcs ^= 1;
        }
    }

    /* 无事件时勿狂写 ERDP——真机 WaitCommand 空转会 MMIO 拖死 */
    if (Progress) {
        UINT64 Erdp = PointerToPhysical(&gEvtRingLive[gEvtDeq]) | (1ULL << 3);
        WriteMmio64(gRuntimeBase + 0x38, Erdp);
        WriteMmio32(gOperationalBase + 4, USBSTS_EINT);
    }
}

void ProcessEvents(void) {
    SpinLockAcquire(&gEvtConsumerLock);
    ProcessEventsLocked();
    SpinLockRelease(&gEvtConsumerLock);
}

/*
 * 真机：USBSTS.EINT 已置但 Cycle 对不上时，仅当环头 Cycle==~CCS 才翻一次。
 * 旧逻辑见 EINT 就翻：PCD/粘住 EINT 会把 CCS 永久弄反 → 中断完成永远吃不到，
 * 却仍可能靠碰巧/翻回来吃到部分 EP0（PHOTO：t>0 i=0）。
 * excl-2：整段持 gEvtConsumerLock，内部只调 Locked（勿再进 ProcessEvents 嵌套锁）。
 */
void ProcessEventsRealPc(void) {
    UINT32 Sts;
    XHCI_TRB *Evt;
    UINT32 EvtSize = gEvtRingSize ? gEvtRingSize : EVT_SIZE;

    SpinLockAcquire(&gEvtConsumerLock);
    ProcessEventsLocked();
    if (gCmdDone) {
        SpinLockRelease(&gEvtConsumerLock);
        return;
    }
    Sts = ReadMmio32(gOperationalBase + 4);
    if (!(Sts & USBSTS_EINT)) {
        SpinLockRelease(&gEvtConsumerLock);
        return;
    }
    if (gEvtDeq >= EvtSize) {
        SpinLockRelease(&gEvtConsumerLock);
        return;
    }
    Evt = &gEvtRingLive[gEvtDeq];
    FlushDma(Evt, sizeof(*Evt));
    if ((Evt->Control & TRB_C) == ((gEvtCcs ^ 1u) & 1u)) {
        gEvtCcs ^= 1u;
        ProcessEventsLocked();
    } else {
        /* 无待处理事件：清粘住的 EINT，勿翻 CCS */
        WriteMmio32(gOperationalBase + 4, USBSTS_EINT);
    }
    SpinLockRelease(&gEvtConsumerLock);
}
