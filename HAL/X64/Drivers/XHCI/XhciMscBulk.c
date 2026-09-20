/*
 * XhciMscBulk.c — PR-S-xhcimsc-1：从 XhciMsc.c 原样搬家；不改语义。
 * MSC 全局仍定义在 Xhci.c（BSS 顺序影响 HID DMA 环；勿迁出）。
 */
#include "XHCI/XhciInternal.h"

static int WaitBulk(void) {
    int Own = 0;
    int Result = -1;

    if (!XhciEventIsExclusive()) {
        XhciEventEnterExclusive();
        Own = 1;
    }

    if (!HalCpuIsHypervisor()) {
        UINT64 T0 = ReadTsc();
        UINT64 Need = 500ULL * 3000000ULL; /* ~500ms：大 U 盘 INQUIRY 可慢 */

        for (;;) {
            ProcessEventsRealPc();
            ServiceHidCompletions();
            if (gBulkDone) {
                Result = (gBulkCode == CC_SUCCESS || gBulkCode == CC_SHORT_PACKET) ? 0
                                                                                : -1;
                break;
            }
            if (ReadTsc() - T0 >= Need) {
                break;
            }
            __asm__ volatile ("pause");
        }
    } else {
        int Timeout = 200000;

        while (Timeout--) {
            ProcessEvents();
            ServiceHidCompletions();
            if (gBulkDone) {
                Result = (gBulkCode == CC_SUCCESS || gBulkCode == CC_SHORT_PACKET) ? 0
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

/*
 * PR-H-msc-5：Bulk 普通传输。DirIn=1 → Bulk IN 环；0 → Bulk OUT。
 * 成功 0；失败 -1。短包算成功（CSW/INQUIRY 常见）。
 */
int XhciBulkXfer(int DirIn, void *Buf, UINT32 Len) {
    XHCI_TRB *Ring;
    RING_STATE *St;
    UINT32 Dci;
    UINT32 Ctrl;

    if (!gMscClaimed || gMscScanSlot == 0 || Buf == 0 || Len == 0) {
        return -1;
    }
    if (gMscBulkInDci == 0 || gMscBulkOutDci == 0) {
        return -1;
    }

    if (DirIn) {
        Ring = gBulkInRing;
        St = &gBulkIn;
        Dci = gMscBulkInDci;
        Ctrl = TRB_TYPE(TRB_NORMAL) | TRB_IOC | TRB_ISP;
    } else {
        Ring = gBulkOutRing;
        St = &gBulkOut;
        Dci = gMscBulkOutDci;
        Ctrl = TRB_TYPE(TRB_NORMAL) | TRB_IOC;
    }

    FlushDma(Buf, Len);
    gBulkDone = 0;
    gBulkCode = 0;
    gBulkRemain = 0;
    /* excl-1：门铃与 WaitBulk 同独占窗 */
    XhciEventEnterExclusive();
    Enqueue(Ring, St, PointerToPhysical(Buf), Len, Ctrl);
    FlushDma(Ring, sizeof(XHCI_TRB) * (St->Size ? St->Size : RING_SIZE));
    RingDoorbell(gMscScanSlot, Dci);
    if (WaitBulk() < 0) {
        XhciEventLeaveExclusive();
        BootLogHex("Boot: MSC bulk fail cc=", gBulkCode, 2);
        return -1;
    }
    XhciEventLeaveExclusive();
    FlushDma(Buf, Len);
    return 0;
}
