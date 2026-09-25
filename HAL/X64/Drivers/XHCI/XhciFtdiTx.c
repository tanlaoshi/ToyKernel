/*
 * XhciFtdiTx.c — PR-H-usb-uart-ftdi-1：FT232 Bulk OUT TX + 事件匹配
 */
#include "XHCI/XhciInternal.h"

extern UINT32 gFtdiBulkOutDci;
extern UINT16 gFtdiBulkOutMps;
extern XHCI_TRB gFtdiBulkOutRing[RING_SIZE];
extern RING_STATE gFtdiBulkOut;

static UINT8 gFtdiTxBuf[64] __attribute__((aligned(64)));
static volatile UINT32 gFtdiBulkDone;
static volatile UINT32 gFtdiBulkCode;
static int gFtdiTxBusy;

int XhciFtdiMatchXferEvent(UINT32 EvtSlot, UINT32 Ep, UINT64 TrbPtr, UINT32 Code) {
    UINT64 Lo;
    UINT64 Hi;

    if (!gFtdiClaimed || gFtdiSlot == 0 || EvtSlot != gFtdiSlot) {
        return 0;
    }
    Lo = PointerToPhysical(gFtdiBulkOutRing);
    Hi = Lo + sizeof(gFtdiBulkOutRing);
    if ((gFtdiBulkOutDci != 0 && Ep == gFtdiBulkOutDci) ||
        (TrbPtr >= Lo && TrbPtr < Hi)) {
        gFtdiBulkCode = Code;
        gFtdiBulkDone = 1;
        return 1;
    }
    return 0;
}

static int WaitFtdiBulk(void) {
    int Own = 0;
    int Result = -1;
    UINT64 T0 = ReadTsc();
    UINT64 Need = 200ULL * 3000000ULL;

    if (!XhciEventIsExclusive()) {
        XhciEventEnterExclusive();
        Own = 1;
    }
    for (;;) {
        if (HalCpuIsHypervisor()) {
            ProcessEvents();
        } else {
            ProcessEventsRealPc();
        }
        ServiceHidCompletions();
        if (gFtdiBulkDone) {
            Result = (gFtdiBulkCode == CC_SUCCESS || gFtdiBulkCode == CC_SHORT_PACKET)
                         ? 0
                         : -1;
            break;
        }
        if (ReadTsc() - T0 >= Need) {
            break;
        }
        __asm__ volatile("pause");
    }
    if (Own) {
        XhciEventLeaveExclusive();
    }
    return Result;
}

void XhciFtdiWrite(const char *Text) {
    UINT32 Mps;
    UINT32 N;

    if (!Text || !gFtdiClaimed || gFtdiSlot == 0 || gFtdiBulkOutDci == 0) {
        return;
    }
    if (gFtdiTxBusy) {
        return;
    }
    gFtdiTxBusy = 1;
    Mps = gFtdiBulkOutMps ? gFtdiBulkOutMps : 64;
    if (Mps > sizeof(gFtdiTxBuf)) {
        Mps = (UINT32)sizeof(gFtdiTxBuf);
    }

    while (*Text) {
        N = 0;
        while (Text[N] && N < Mps) {
            gFtdiTxBuf[N] = (UINT8)Text[N];
            N++;
        }
        Text += N;
        FlushDma(gFtdiTxBuf, N);
        gFtdiBulkDone = 0;
        gFtdiBulkCode = 0;
        XhciEventEnterExclusive();
        Enqueue(gFtdiBulkOutRing, &gFtdiBulkOut, PointerToPhysical(gFtdiTxBuf), N,
                TRB_TYPE(TRB_NORMAL) | TRB_IOC);
        FlushDma(gFtdiBulkOutRing, sizeof(gFtdiBulkOutRing));
        RingDoorbell(gFtdiSlot, gFtdiBulkOutDci);
        if (WaitFtdiBulk() < 0) {
            XhciEventLeaveExclusive();
            break;
        }
        XhciEventLeaveExclusive();
    }
    gFtdiTxBusy = 0;
}
