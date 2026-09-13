/*
 * XhciTransfer.c — PR-H-xhci-core-split-4：WaitTransfer / ServiceHidCompletions
 * PR-H-xhci-evt-excl-1：Wait 全程独占（门铃由 ControlXfer 同窗敲）
 */
#include "XHCI/XhciInternal.h"

/* 枚举期 Wait* 也会进 ProcessEvents；必须顺带再投递中断 IN，否则 TRB 耗尽后永久无完成 */
void ServiceHidCompletions(void) {
    if (gIntrDone) {
        gIntrDone = 0;
        if (gIntrReportReady) {
            gIntrReportReady = 0;
            FlushDma(gReportBuf, sizeof(gReportBuf));
            KbdPush();
            gStatKbdPush++;
        }
        if (gSlotId != 0 && gIntrDci != 0) {
            QueueIntr();
        }
    }
    if (gMouseIntrDone) {
        gMouseIntrDone = 0;
        if (gMouseReportReady) {
            gMouseReportReady = 0;
            FlushDma(gMouseBuf, sizeof(gMouseBuf));
            MousePush();
            gStatMousePush++;
        }
        if (gMouseSlotId != 0 && gMouseIntrDci != 0) {
            QueueMouseIntr();
        }
    }
}

int WaitTransfer(int Timeout) {
    int Own = 0;
    int Result = -1;

    if (!XhciEventIsExclusive()) {
        XhciEventEnterExclusive();
        Own = 1;
    }

    /*
     * 真机：按 TSC 限时（默认 ~80ms）。旧版固定 20 万次 ProcessEvents，
     * 多口 ControlXfer 超时会空转数十秒 → 短按电源无效、只能长按硬关。
     */
    if (!HalCpuIsHypervisor()) {
        UINT64 T0 = ReadTsc();
        UINT64 Need = gXferFast ? (25ULL * 3000000ULL) : (150ULL * 3000000ULL);
        for (;;) {
            ProcessEventsRealPc();
            ServiceHidCompletions();
            if (gXferDone) {
                Result = (gXferCode == CC_SUCCESS || gXferCode == CC_SHORT_PACKET) ? 0
                                                                                   : -1;
                break;
            }
            if (ReadTsc() - T0 >= Need) {
                break;
            }
            __asm__ volatile ("pause");
        }
    } else {
        while (Timeout--) {
            ProcessEvents();
            ServiceHidCompletions();
            if (gXferDone) {
                Result = (gXferCode == CC_SUCCESS || gXferCode == CC_SHORT_PACKET) ? 0
                                                                                   : -1;
                break;
            }
        }
    }

    if (Own) {
        XhciEventLeaveExclusive();
    }
    return Result;
}
