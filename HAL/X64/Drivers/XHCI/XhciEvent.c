/*
 * XhciEvent.c — PR-H-xhci-msc-split-2：事件环 ProcessEvents*
 * PR-H-xhci-evt-excl-1：独占窗 API（门铃/Wait 同消费者）
 * PR-H-xhci-evt-excl-2：gEvtConsumerLock + ProcessEventsLocked
 *
 * DMA/环全局仍在 XhciCore.c（勿迁 BSS）。
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

        UINT32 Type = TrbType(Evt->Control);
        UINT32 Code = (Evt->Status >> 24) & 0xFF;
        UINT32 Slot = (Evt->Control >> 24) & 0xFF;

        if (Type == TRB_CMD_COMPLETION) {
            gCmdCode = Code;
            gCmdSlot = Slot;
            gCmdDone = 1;
        } else if (Type == TRB_TRANSFER_EVENT) {
            UINT32 Ep = (Evt->Control >> 16) & 0x1F;
            UINT32 EvtSlot = (Evt->Control >> 24) & 0xFF;
            UINT64 TrbPtr = Evt->Parameter & ~0xFULL;
            UINT64 KbdLo = PointerToPhysical(gIntrRing);
            UINT64 KbdHi = KbdLo + sizeof(gIntrRing);
            UINT64 MouseLo = PointerToPhysical(gMouseIntrRing);
            UINT64 MouseHi = MouseLo + sizeof(gMouseIntrRing);
            int Matched = 0;
            int KbdHit = 0;
            int MouseHit = 0;

            gStatXferAny++;
            gStatLastCc = Code;
            gStatLastSlot = EvtSlot;
            gStatLastEp = Ep;

            /* EP0(DCI=1) 才唤醒 WaitTransfer，避免 HID IN 误完成 EP0 等待 */
            if (EvtSlot == gXferSlot && Ep == 1) {
                gXferCode = Code;
                gXferRemain = Evt->Status & 0xFFFFFF;
                gXferDone = 1;
                Matched = 1; /* GET_REPORT/控制传输：勿记入 unmatched 刷屏 */
            }
            /* PR-H-msc-5：Bulk 完成（slot+DCI 或 TRB 落在 Bulk 环） */
            if (!Matched && gMscScanSlot != 0 && EvtSlot == gMscScanSlot &&
                ((gMscBulkInDci != 0 && Ep == gMscBulkInDci) ||
                 (gMscBulkOutDci != 0 && Ep == gMscBulkOutDci))) {
                gBulkCode = Code;
                gBulkRemain = Evt->Status & 0xFFFFFF;
                gBulkDone = 1;
                Matched = 1;
            }
            if (!Matched) {
                UINT64 BulkInLo = PointerToPhysical(gBulkInRing);
                UINT64 BulkInHi = BulkInLo + sizeof(gBulkInRing);
                UINT64 BulkOutLo = PointerToPhysical(gBulkOutRing);
                UINT64 BulkOutHi = BulkOutLo + sizeof(gBulkOutRing);

                if ((TrbPtr >= BulkInLo && TrbPtr < BulkInHi) ||
                    (TrbPtr >= BulkOutLo && TrbPtr < BulkOutHi)) {
                    gBulkCode = Code;
                    gBulkRemain = Evt->Status & 0xFFFFFF;
                    gBulkDone = 1;
                    Matched = 1;
                }
            }
            /*
             * 中断 EP：只认 slot+DCI，或完成 TRB 落在中断环内。
             * 勿用 EpNum（易与 EP0 的 EndpointID=1 撞）或报告缓冲指针
             * （GET_REPORT 数据 TRB 也指向报告区 → 假 i=、干扰推送）。
             */
            {
                KbdHit = (gSlotId != 0 && gIntrDci != 0 && Ep != 1 &&
                          ((EvtSlot == gSlotId && Ep == gIntrDci) ||
                           (TrbPtr >= KbdLo && TrbPtr < KbdHi)));
                MouseHit = (gMouseSlotId != 0 && gMouseIntrDci != 0 && Ep != 1 &&
                            ((EvtSlot == gMouseSlotId && Ep == gMouseIntrDci) ||
                             (TrbPtr >= MouseLo && TrbPtr < MouseHi)));
            }
            if (KbdHit) {
                Matched = 1;
                if (Code == CC_SUCCESS || Code == CC_SHORT_PACKET) {
                    gStatIntrEvt++;
                    gIntrReportReady = 1;
                    gIntrDone = 1;
                } else if (Code == CC_STOPPED || Code == CC_STOPPED_LEN ||
                           Code == CC_STOPPED_SHORT) {
                    /*
                     * Stop EP 的副作用：勿 gIntrDone/QueueIntr，否则 Arm 同步时
                     * 会在 SetTrDeq 前再敲门铃 → Context State Error (0x13)。
                     */
                } else {
                    gStatIntrEvt++;
                    gIntrDone = 1; /* 其它错误：允许重投 */
                    if (gDiagIntrCcLogged < 4) {
                        char Line[64];
                        int n = 0;
                        const char *P = "boot: xhci kbd-intr cc=";
                        while (*P && n < 28) {
                            Line[n++] = *P++;
                        }
                        Line[n++] = (char)('0' + ((Code / 10) % 10));
                        Line[n++] = (char)('0' + (Code % 10));
                        Line[n++] = '\n';
                        Line[n] = 0;
                        ToyLogUsb(Line);
                        gDiagIntrCcLogged++;
                    }
                }
            }
            if (MouseHit) {
                Matched = 1;
                if (Code == CC_SUCCESS || Code == CC_SHORT_PACKET) {
                    UINT32 Remain = Evt->Status & 0xFFFFFF;
                    UINT32 Req = gMouseReportLen ? gMouseReportLen : 8;

                    gStatMouseEvt++;
                    gMouseReportReady = 1;
                    gMouseIntrDone = 1;
                    /* 短包：Remain=未传完；实际长度=请求-Remain */
                    if (Remain < Req) {
                        gMouseXferLen = (UINT8)(Req - Remain);
                    } else {
                        gMouseXferLen = (UINT8)Req;
                    }
                    if (gMouseXferLen < 3) {
                        gMouseXferLen = 3;
                    }
                    if (gMouseXferLen > 8) {
                        gMouseXferLen = 8;
                    }
                } else if (Code == CC_STOPPED || Code == CC_STOPPED_LEN ||
                           Code == CC_STOPPED_SHORT) {
                    /* 同上：Stop 取消，勿重投门铃 */
                } else {
                    gStatMouseEvt++;
                    gMouseIntrDone = 1;
                    if (gDiagIntrCcLogged < 4) {
                        char Line[64];
                        int n = 0;
                        const char *P = "boot: xhci mouse-intr cc=";
                        while (*P && n < 30) {
                            Line[n++] = *P++;
                        }
                        Line[n++] = (char)('0' + ((Code / 10) % 10));
                        Line[n++] = (char)('0' + (Code % 10));
                        Line[n++] = '\n';
                        Line[n] = 0;
                        ToyLogUsb(Line);
                        gDiagIntrCcLogged++;
                    }
                }
            }
            if (!Matched) {
                gStatUnmatched++;
                if (gDiagXferLogged < 8) {
                    char Line[80];
                    int n = 0;
                    const char *P = "boot: xhci xfer s=";
                    while (*P && n < 24) {
                        Line[n++] = *P++;
                    }
                    Line[n++] = (char)('0' + ((EvtSlot / 10) % 10));
                    Line[n++] = (char)('0' + (EvtSlot % 10));
                    Line[n++] = ' ';
                    Line[n++] = 'e';
                    Line[n++] = '=';
                    Line[n++] = (char)('0' + ((Ep / 10) % 10));
                    Line[n++] = (char)('0' + (Ep % 10));
                    Line[n++] = ' ';
                    Line[n++] = 'c';
                    Line[n++] = '=';
                    Line[n++] = (char)('0' + ((Code / 10) % 10));
                    Line[n++] = (char)('0' + (Code % 10));
                    Line[n++] = '\n';
                    Line[n] = 0;
                    ToyLogUsb(Line);
                    gDiagXferLogged++;
                }
            }
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
