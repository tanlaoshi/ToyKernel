/*
 * XhciFtdiRx.c — PR-H-usb-uart-ftdi-2：Bulk IN + 剥 2 字节状态 → 字符环
 */
#include "XHCI/XhciInternal.h"

#define FTDI_RX_Q   256
#define FTDI_STATUS 2

extern UINT32 gFtdiBulkOutDci;
extern UINT16 gFtdiBulkOutMps;
extern XHCI_TRB gFtdiBulkOutRing[RING_SIZE];
extern RING_STATE gFtdiBulkOut;
extern UINT8 gFtdiDevCtx[2048];
extern UINT32 gFtdiRoute;
extern UINT8 gFtdiHubSlot;
extern UINT8 gFtdiTtPort;

UINT32 gFtdiBulkInDci;
UINT16 gFtdiBulkInMps;
XHCI_TRB gFtdiBulkInRing[RING_SIZE] __attribute__((aligned(64)));
RING_STATE gFtdiBulkIn;

static UINT8 gFtdiRxDma[64] __attribute__((aligned(64)));
static char gFtdiRxQ[FTDI_RX_Q];
static UINT32 gFtdiRxR;
static UINT32 gFtdiRxW;
static volatile UINT32 gFtdiInDone;
static volatile UINT32 gFtdiInCode;
static volatile UINT32 gFtdiInRemain;
static UINT32 gFtdiInXferLen;
static int gFtdiInArmed;
static int gFtdiRxBusy;

static void RxPush(char C) {
    UINT32 Next = (gFtdiRxW + 1u) % FTDI_RX_Q;

    if (Next == gFtdiRxR) {
        return; /* 满则丢，勿堵 USB */
    }
    gFtdiRxQ[gFtdiRxW] = C;
    gFtdiRxW = Next;
}

int XhciFtdiDataReady(void) {
    return (gFtdiClaimed && gFtdiRxR != gFtdiRxW) ? 1 : 0;
}

char XhciFtdiReadChar(void) {
    char C;

    if (gFtdiRxR == gFtdiRxW) {
        return 0;
    }
    C = gFtdiRxQ[gFtdiRxR];
    gFtdiRxR = (gFtdiRxR + 1u) % FTDI_RX_Q;
    return C;
}

int XhciFtdiOnInComplete(UINT32 Code, UINT32 Remain) {
    gFtdiInCode = Code;
    gFtdiInRemain = Remain;
    gFtdiInDone = 1;
    return 1;
}

/*
 * ConfigEP：Bulk IN + OUT（ftdi-1 OUT + ftdi-2 IN 一次配齐）。
 */
int XhciFtdiConfigBulk(UINT32 SlotId, UINT32 RootPort, UINT8 Speed,
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
    Slot[0] = (CtxEntries << 27) | ((UINT32)Speed << 20) | (gFtdiRoute & 0xFFFFFu);
    Slot[1] = (UINT32)RootPort << 16;
    if (gFtdiHubSlot != 0 && Speed < 3) {
        Slot[2] = (UINT32)gFtdiHubSlot | ((UINT32)gFtdiTtPort << 8);
    }

    InitRing(gFtdiBulkInRing, &gFtdiBulkIn, RING_SIZE);
    InitRing(gFtdiBulkOutRing, &gFtdiBulkOut, RING_SIZE);

    Ep = (UINT32 *)(void *)InEp(InDci);
    Ep[0] = 0;
    Ep[1] = (3u << 1) | (6u << 3) | ((UINT32)MpsIn << 16);
    Deq = PointerToPhysical(gFtdiBulkInRing) | 1;
    Ep[2] = (UINT32)Deq;
    Ep[3] = (UINT32)(Deq >> 32);
    Ep[4] = (UINT32)MpsIn;

    Ep = (UINT32 *)(void *)InEp(OutDci);
    Ep[0] = 0;
    Ep[1] = (3u << 1) | (2u << 3) | ((UINT32)MpsOut << 16);
    Deq = PointerToPhysical(gFtdiBulkOutRing) | 1;
    Ep[2] = (UINT32)Deq;
    Ep[3] = (UINT32)(Deq >> 32);
    Ep[4] = (UINT32)MpsOut;

    FlushDma(gInCtx, sizeof(gInCtx));
    FlushDma(gFtdiBulkInRing, sizeof(gFtdiBulkInRing));
    FlushDma(gFtdiBulkOutRing, sizeof(gFtdiBulkOutRing));
    FlushDma(gFtdiDevCtx, sizeof(gFtdiDevCtx));
    if (Command(PointerToPhysical(gInCtx), TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(SlotId), 0) <
        0) {
        BootLog("Boot: usb-uart cfg ep fail\n");
        return 0;
    }
    gFtdiBulkInDci = InDci;
    gFtdiBulkOutDci = OutDci;
    gFtdiBulkInMps = MpsIn;
    gFtdiBulkOutMps = MpsOut;
    gFtdiInArmed = 0;
    gFtdiInDone = 0;
    return 1;
}

static void QueueBulkIn(void) {
    UINT32 Len = gFtdiBulkInMps ? gFtdiBulkInMps : 64;

    if (Len > sizeof(gFtdiRxDma)) {
        Len = (UINT32)sizeof(gFtdiRxDma);
    }
    gFtdiInXferLen = Len;
    gFtdiInDone = 0;
    gFtdiInCode = 0;
    gFtdiInRemain = 0;
    FlushDma(gFtdiRxDma, Len);
    XhciEventEnterExclusive();
    Enqueue(gFtdiBulkInRing, &gFtdiBulkIn, PointerToPhysical(gFtdiRxDma), Len,
            TRB_TYPE(TRB_NORMAL) | TRB_IOC | TRB_ISP);
    FlushDma(gFtdiBulkInRing, sizeof(gFtdiBulkInRing));
    RingDoorbell(gFtdiSlot, gFtdiBulkInDci);
    XhciEventLeaveExclusive();
    gFtdiInArmed = 1;
}

static void DrainInCompletion(void) {
    UINT32 Got;
    UINT32 i;

    if (!gFtdiInDone) {
        return;
    }
    gFtdiInDone = 0;
    gFtdiInArmed = 0;
    if (gFtdiInCode != CC_SUCCESS && gFtdiInCode != CC_SHORT_PACKET) {
        return;
    }
    Got = gFtdiInXferLen;
    if (gFtdiInRemain <= Got) {
        Got = Got - gFtdiInRemain;
    }
    FlushDma(gFtdiRxDma, Got ? Got : 1);
    /* FTDI：前 2 字节状态；仅状态包时 Got==2 → 无载荷 */
    if (Got > FTDI_STATUS) {
        for (i = FTDI_STATUS; i < Got; i++) {
            RxPush((char)gFtdiRxDma[i]);
        }
    }
}

void XhciFtdiPollRx(void) {
    int Own = 0;

    if (!gFtdiClaimed || gFtdiSlot == 0 || gFtdiBulkInDci == 0) {
        return;
    }
    if (gFtdiRxBusy) {
        return;
    }
    gFtdiRxBusy = 1;

    if (!XhciEventIsExclusive()) {
        XhciEventEnterExclusive();
        Own = 1;
    }
    if (HalCpuIsHypervisor()) {
        ProcessEvents();
    } else {
        ProcessEventsRealPc();
    }
    ServiceHidCompletions();
    if (Own) {
        XhciEventLeaveExclusive();
    }

    DrainInCompletion();
    if (!gFtdiInArmed) {
        QueueBulkIn();
    }
    gFtdiRxBusy = 0;
}

void XhciFtdiRxArm(void) {
    gFtdiRxR = 0;
    gFtdiRxW = 0;
    gFtdiInArmed = 0;
    gFtdiInDone = 0;
    if (gFtdiClaimed && gFtdiBulkInDci != 0) {
        QueueBulkIn();
    }
}
