/*
 * XhciCdcTx.c — PR-H-usb-uart-cdc-1：CDC Bulk OUT TX + ConfigEP + 事件匹配
 */
#include "XHCI/XhciInternal.h"

extern UINT32 gCdcBulkOutDci;
extern UINT16 gCdcBulkOutMps;
extern UINT32 gCdcBulkInDci;
extern UINT16 gCdcBulkInMps;
extern UINT32 gCdcRoute;
extern UINT8 gCdcHubSlot;
extern UINT8 gCdcTtPort;
extern UINT8 gCdcDevCtx[2048];
extern XHCI_TRB gCdcBulkOutRing[RING_SIZE];
extern RING_STATE gCdcBulkOut;
extern XHCI_TRB gCdcBulkInRing[RING_SIZE];
extern RING_STATE gCdcBulkIn;

static UINT8 gCdcTxBuf[64] __attribute__((aligned(64)));
static volatile UINT32 gCdcBulkDone;
static volatile UINT32 gCdcBulkCode;
static int gCdcTxBusy;

int XhciCdcConfigBulk(UINT32 SlotId, UINT32 RootPort, UINT8 Speed,
                      UINT8 EpIn, UINT16 MpsIn, UINT8 EpOut, UINT16 MpsOut) {
    UINT8 InNum = EpIn & 0x0F;
    UINT8 OutNum = EpOut & 0x0F;
    UINT32 InDci = (UINT32)InNum * 2 + 1;
    UINT32 OutDci = (UINT32)OutNum * 2 + 0;
    UINT32 CtxEntries = InDci > OutDci ? InDci : OutDci;
    UINT32 *Slot;
    UINT32 *Ep;
    UINT64 Deq;

    if (MpsIn == 0 || MpsIn > 512) {
        MpsIn = 64;
    }
    if (MpsOut == 0 || MpsOut > 512) {
        MpsOut = 64;
    }

    ZeroMemory(gInCtx, sizeof(gInCtx));
    *(UINT32 *)(void *)(gInCtx + 4) = (1u << 0) | (1u << InDci) | (1u << OutDci);

    Slot = (UINT32 *)(void *)InSlot();
    Slot[0] = (CtxEntries << 27) | ((UINT32)Speed << 20) | (gCdcRoute & 0xFFFFFu);
    Slot[1] = (UINT32)RootPort << 16;
    if (gCdcHubSlot != 0 && Speed < 3) {
        Slot[2] = (UINT32)gCdcHubSlot | ((UINT32)gCdcTtPort << 8);
    }

    InitRing(gCdcBulkInRing, &gCdcBulkIn, RING_SIZE);
    InitRing(gCdcBulkOutRing, &gCdcBulkOut, RING_SIZE);

    Ep = (UINT32 *)(void *)InEp(InDci);
    Ep[0] = 0;
    Ep[1] = (3u << 1) | (6u << 3) | ((UINT32)MpsIn << 16);
    Deq = PointerToPhysical(gCdcBulkInRing) | 1;
    Ep[2] = (UINT32)Deq;
    Ep[3] = (UINT32)(Deq >> 32);
    Ep[4] = (UINT32)MpsIn;

    Ep = (UINT32 *)(void *)InEp(OutDci);
    Ep[0] = 0;
    Ep[1] = (3u << 1) | (2u << 3) | ((UINT32)MpsOut << 16);
    Deq = PointerToPhysical(gCdcBulkOutRing) | 1;
    Ep[2] = (UINT32)Deq;
    Ep[3] = (UINT32)(Deq >> 32);
    Ep[4] = (UINT32)MpsOut;

    FlushDma(gInCtx, sizeof(gInCtx));
    FlushDma(gCdcBulkInRing, sizeof(gCdcBulkInRing));
    FlushDma(gCdcBulkOutRing, sizeof(gCdcBulkOutRing));
    FlushDma(gCdcDevCtx, sizeof(gCdcDevCtx));
    if (Command(PointerToPhysical(gInCtx), TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(SlotId), 0) <
        0) {
        BootLog("Boot: USB-UART CDC Cfg Ep Fail\n");
        return 0;
    }
    gCdcBulkInDci = InDci;
    gCdcBulkOutDci = OutDci;
    gCdcBulkInMps = MpsIn;
    gCdcBulkOutMps = MpsOut;
    return 1;
}

int XhciCdcOnInComplete(UINT32 Code, UINT32 Remain);

int XhciCdcMatchXferEvent(UINT32 EvtSlot, UINT32 Ep, UINT64 TrbPtr, UINT32 Code,
                          UINT32 Remain) {
    UINT64 OutLo;
    UINT64 OutHi;
    UINT64 InLo;
    UINT64 InHi;

    if (!gCdcClaimed || gCdcSlot == 0 || EvtSlot != gCdcSlot) {
        return 0;
    }
    OutLo = PointerToPhysical(gCdcBulkOutRing);
    OutHi = OutLo + sizeof(gCdcBulkOutRing);
    InLo = PointerToPhysical(gCdcBulkInRing);
    InHi = InLo + sizeof(gCdcBulkInRing);
    if ((gCdcBulkOutDci != 0 && Ep == gCdcBulkOutDci) ||
        (TrbPtr >= OutLo && TrbPtr < OutHi)) {
        gCdcBulkCode = Code;
        gCdcBulkDone = 1;
        return 1;
    }
    if ((gCdcBulkInDci != 0 && Ep == gCdcBulkInDci) ||
        (TrbPtr >= InLo && TrbPtr < InHi)) {
        return XhciCdcOnInComplete(Code, Remain);
    }
    return 0;
}

static int WaitCdcBulk(void) {
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
        if (gCdcBulkDone) {
            Result = (gCdcBulkCode == CC_SUCCESS || gCdcBulkCode == CC_SHORT_PACKET)
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

void XhciCdcWrite(const char *Text) {
    UINT32 Mps;
    UINT32 N;

    if (!Text || !gCdcClaimed || gCdcSlot == 0 || gCdcBulkOutDci == 0) {
        return;
    }
    if (gCdcTxBusy) {
        return;
    }
    gCdcTxBusy = 1;
    Mps = gCdcBulkOutMps ? gCdcBulkOutMps : 64;
    if (Mps > sizeof(gCdcTxBuf)) {
        Mps = (UINT32)sizeof(gCdcTxBuf);
    }

    while (*Text) {
        N = 0;
        while (Text[N] && N < Mps) {
            gCdcTxBuf[N] = (UINT8)Text[N];
            N++;
        }
        Text += N;
        FlushDma(gCdcTxBuf, N);
        gCdcBulkDone = 0;
        gCdcBulkCode = 0;
        XhciEventEnterExclusive();
        Enqueue(gCdcBulkOutRing, &gCdcBulkOut, PointerToPhysical(gCdcTxBuf), N,
                TRB_TYPE(TRB_NORMAL) | TRB_IOC);
        FlushDma(gCdcBulkOutRing, sizeof(gCdcBulkOutRing));
        RingDoorbell(gCdcSlot, gCdcBulkOutDci);
        if (WaitCdcBulk() < 0) {
            XhciEventLeaveExclusive();
            break;
        }
        XhciEventLeaveExclusive();
    }
    gCdcTxBusy = 0;
}
