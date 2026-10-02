/*
 * XhciEventXfer.c — Transfer Event 分发（PR-F-xhci-3）
 */
#include "XHCI/XhciInternal.h"

void XhciEventHandleTransfer(XHCI_TRB *Evt) {
    UINT32 Code = (Evt->Status >> 24) & 0xFF;
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
        gXferRemain = Evt->Status & XHCI_TRB_REMAIN_MASK;
        gXferDone = 1;
        Matched = 1; /* GET_REPORT/控制传输：勿记入 unmatched 刷屏 */
    }
    /* PR-H-msc-5：Bulk 完成（slot+DCI 或 TRB 落在 Bulk 环） */
    if (!Matched && gMscScanSlot != 0 && EvtSlot == gMscScanSlot &&
        ((gMscBulkInDci != 0 && Ep == gMscBulkInDci) ||
         (gMscBulkOutDci != 0 && Ep == gMscBulkOutDci))) {
        gBulkCode = Code;
        gBulkRemain = Evt->Status & XHCI_TRB_REMAIN_MASK;
        gBulkDone = 1;
        Matched = 1;
    }
    if (!Matched && XhciFtdiMatchXferEvent(EvtSlot, Ep, TrbPtr, Code,
                                           Evt->Status & 0xFFFFFFu)) {
        Matched = 1;
    }
    if (!Matched && XhciCdcMatchXferEvent(EvtSlot, Ep, TrbPtr, Code,
                                           Evt->Status & 0xFFFFFFu)) {
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
            gBulkRemain = Evt->Status & XHCI_TRB_REMAIN_MASK;
            gBulkDone = 1;
            Matched = 1;
        }
    }
    /*
     * 中断 EP：只认 slot+DCI，或完成 TRB 落在中断环内。
     * 勿用 EpNum（易与 EP0 的 EndpointID=1 撞）或报告缓冲指针
     * （GET_REPORT 数据 TRB 也指向报告区 → 假 i=、干扰推送）。
     */
    KbdHit = (gSlotId != 0 && gIntrDci != 0 && Ep != 1 &&
              ((EvtSlot == gSlotId && Ep == gIntrDci) ||
               (TrbPtr >= KbdLo && TrbPtr < KbdHi)));
    MouseHit = (gMouseSlotId != 0 && gMouseIntrDci != 0 && Ep != 1 &&
                ((EvtSlot == gMouseSlotId && Ep == gMouseIntrDci) ||
                 (TrbPtr >= MouseLo && TrbPtr < MouseHi)));
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
                int N = 0;
                const char *P = "Boot: XHCI kbd-intr cc=";
                while (*P && N < 28) {
                    Line[N++] = *P++;
                }
                Line[N++] = (char)('0' + ((Code / 10) % 10));
                Line[N++] = (char)('0' + (Code % 10));
                Line[N++] = '\n';
                Line[N] = 0;
                ToyLogUsb(Line);
                gDiagIntrCcLogged++;
            }
        }
    }
    if (MouseHit) {
        Matched = 1;
        if (Code == CC_SUCCESS || Code == CC_SHORT_PACKET) {
            UINT32 Remain = Evt->Status & XHCI_TRB_REMAIN_MASK;
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
                int N = 0;
                const char *P = "Boot: XHCI mouse-intr cc=";
                while (*P && N < 30) {
                    Line[N++] = *P++;
                }
                Line[N++] = (char)('0' + ((Code / 10) % 10));
                Line[N++] = (char)('0' + (Code % 10));
                Line[N++] = '\n';
                Line[N] = 0;
                ToyLogUsb(Line);
                gDiagIntrCcLogged++;
            }
        }
    }
    if (!Matched) {
        gStatUnmatched++;
        if (gDiagXferLogged < 8) {
            char Line[80];
            int N = 0;
            const char *P = "Boot: XHCI xfer s=";
            while (*P && N < 24) {
                Line[N++] = *P++;
            }
            Line[N++] = (char)('0' + ((EvtSlot / 10) % 10));
            Line[N++] = (char)('0' + (EvtSlot % 10));
            Line[N++] = ' ';
            Line[N++] = 'e';
            Line[N++] = '=';
            Line[N++] = (char)('0' + ((Ep / 10) % 10));
            Line[N++] = (char)('0' + (Ep % 10));
            Line[N++] = ' ';
            Line[N++] = 'c';
            Line[N++] = '=';
            Line[N++] = (char)('0' + ((Code / 10) % 10));
            Line[N++] = (char)('0' + (Code % 10));
            Line[N++] = '\n';
            Line[N] = 0;
            ToyLogUsb(Line);
            gDiagXferLogged++;
        }
    }
}
