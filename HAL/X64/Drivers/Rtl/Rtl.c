/*
 * Rtl.c — TX/RX 环 + SendFrame/Poll（PR-N-rtl-2）
 */
#include "Rtl.h"
#include "RtlPrivate.h"
#include "PhysicalMemory.h"
#include "Net.h"
#include "Hal.h"
#include "ToySerialLog.h"

static RTL_DESC *gTxDesc;
static RTL_DESC *gRxDesc;
static UINT8 *gTxBufs;
static UINT8 *gRxBufs;
static UINT64 gRingPhys;
static UINT16 gTxIdx;
static UINT16 gRxIdx;
UINT32 gRtlLinkMbps;
int gRtlLinkFull;

static UINT64 TxBufPhys(UINT16 Idx) {
    return gRingPhys + 2u * PAGE_SIZE + (UINT64)Idx * RTL_BUF_SIZE;
}

static UINT64 RxBufPhys(UINT16 Idx) {
    return gRingPhys + 2u * PAGE_SIZE +
           (UINT64)RTL_RING_COUNT * RTL_BUF_SIZE + (UINT64)Idx * RTL_BUF_SIZE;
}

static void InitDescs(void) {
    UINT32 i;
    UINT32 Opts;

    gTxIdx = 0;
    gRxIdx = 0;
    for (i = 0; i < RTL_RING_COUNT; i++) {
        Opts = RTL_BUF_SIZE;
        if (i + 1u == RTL_RING_COUNT) {
            Opts |= RTL_DESC_EOR;
        }
        gTxDesc[i].Opts1 = Opts;
        gTxDesc[i].Opts2 = 0;
        gTxDesc[i].Addr = TxBufPhys((UINT16)i);

        Opts = RTL_DESC_OWN | RTL_BUF_SIZE;
        if (i + 1u == RTL_RING_COUNT) {
            Opts |= RTL_DESC_EOR;
        }
        gRxDesc[i].Opts1 = Opts;
        gRxDesc[i].Opts2 = 0;
        gRxDesc[i].Addr = RxBufPhys((UINT16)i);
    }
    RtlFence();
}

int RtlBringUp(void) {
    UINT8 *Mem;
    UINT32 Pages;
    UINT32 Mbps = 0;
    int Full = 1;

    gRtlLinkMbps = 0;
    gRtlLinkFull = 1;

    if (!RtlHwReset()) {
        ToyLogNet("Boot: R8169 Reset Fail\n");
        return 0;
    }
    RtlHwSetMac(gRtlMac);

    /* TxDesc 页 + RxDesc 页 + TxBufs + RxBufs */
    Pages = 2u + ((RTL_RING_COUNT * RTL_BUF_SIZE * 2u + PAGE_SIZE - 1u) / PAGE_SIZE);
    Mem = (UINT8 *)PhysicalMemoryAllocatePages(Pages);
    if (!Mem) {
        ToyLogNet("Boot: R8169 Ring OOM\n");
        return 0;
    }
    RtlZero(Mem, (UINTN)Pages * PAGE_SIZE);
    gRingPhys = (UINT64)(UINTN)Mem;
    gTxDesc = (RTL_DESC *)(UINTN)Mem;
    gRxDesc = (RTL_DESC *)(UINTN)(Mem + PAGE_SIZE);
    gTxBufs = Mem + 2u * PAGE_SIZE;
    gRxBufs = gTxBufs + RTL_RING_COUNT * RTL_BUF_SIZE;

    InitDescs();
    RtlHwConfigure(gRingPhys, gRingPhys + PAGE_SIZE);

    if (!RtlHwLinkUp(&Mbps, &Full)) {
        ToyLogNet("Boot: R8169 Link Timeout\n");
        return 0;
    }
    gRtlLinkMbps = Mbps;
    gRtlLinkFull = Full;
    RtlHwStart();
    ToyLogNet("Boot: R8169 Link Up\n");
    return 1;
}

int RtlSendFrame(const UINT8 *Frame, UINTN Len) {
    RTL_DESC *D;
    UINTN Wire = Len;
    UINT8 *Buf;
    UINT32 Opts;
    int Spin;

    if (!gRtlReady || !Frame || Len < 14 || !gTxDesc) {
        return -1;
    }
    if (Wire > RTL_BUF_SIZE) {
        return -1;
    }
    if (Wire < 60) {
        Wire = 60;
    }

    D = &gTxDesc[gTxIdx];
    Spin = 100000;
    while (Spin-- > 0 && (D->Opts1 & RTL_DESC_OWN)) {
        HalCpuRelax();
    }
    if (D->Opts1 & RTL_DESC_OWN) {
        return -2;
    }

    Buf = gTxBufs + (UINTN)gTxIdx * RTL_BUF_SIZE;
    RtlZero(Buf, Wire);
    RtlCopy(Buf, Frame, Len);
    D->Addr = TxBufPhys(gTxIdx);
    Opts = RTL_DESC_OWN | RTL_DESC_FS | RTL_DESC_LS | (UINT32)Wire;
    if (gTxIdx + 1u == RTL_RING_COUNT) {
        Opts |= RTL_DESC_EOR;
    }
    D->Opts2 = 0;
    RtlFence();
    D->Opts1 = Opts;
    RtlFence();
    RtlMmioW8(RTL_TXPOLL, RTL_TXPOLL_NPQ);
    gTxIdx = (UINT16)((gTxIdx + 1u) % RTL_RING_COUNT);
    return 0;
}

void RtlPoll(void) {
    int Left = (int)RTL_RING_COUNT + 2;

    if (!gRtlReady || !gRxDesc) {
        return;
    }
    (void)RtlMmioR16(RTL_INTRSTATUS);
    RtlMmioW16(RTL_INTRSTATUS, 0xFFFFu);

    while (Left-- > 0) {
        RTL_DESC *D = &gRxDesc[gRxIdx];
        UINT32 Opts1;
        UINT32 PktLen;
        UINT32 ReOwn;

        Opts1 = D->Opts1;
        if (Opts1 & RTL_DESC_OWN) {
            break;
        }
        PktLen = Opts1 & RTL_DESC_LEN_MASK;
        if (PktLen > RTL_ETH_FCS_LEN + 14u && PktLen <= RTL_BUF_SIZE) {
            UINT8 *Buf = gRxBufs + (UINTN)gRxIdx * RTL_BUF_SIZE;
            NetInputFrame(Buf, (UINTN)(PktLen - RTL_ETH_FCS_LEN));
        }

        ReOwn = RTL_DESC_OWN | RTL_BUF_SIZE;
        if (gRxIdx + 1u == RTL_RING_COUNT) {
            ReOwn |= RTL_DESC_EOR;
        }
        D->Addr = RxBufPhys(gRxIdx);
        D->Opts2 = 0;
        RtlFence();
        D->Opts1 = ReOwn;
        gRxIdx = (UINT16)((gRxIdx + 1u) % RTL_RING_COUNT);
    }
}

int RtlGetLink(int *UpOut, UINT32 *MbpsOut, int *FullDuplexOut) {
    UINT8 St;

    if (!gRtlReady) {
        return -1;
    }
    St = RtlMmioR8(RTL_PHYSTATUS);
    if (St & RTL_PHY_LINK) {
        if ((St & 0x10u) != 0) {
            gRtlLinkMbps = 1000;
        } else if ((St & 0x08u) != 0) {
            gRtlLinkMbps = 100;
        } else if ((St & 0x04u) != 0) {
            gRtlLinkMbps = 10;
        } else {
            gRtlLinkMbps = 100;
        }
        gRtlLinkFull = (St & 0x01u) ? 1 : 0;
    } else {
        gRtlLinkMbps = 0;
    }
    if (UpOut) {
        *UpOut = (gRtlLinkMbps != 0) ? 1 : 0;
    }
    if (MbpsOut) {
        *MbpsOut = gRtlLinkMbps;
    }
    if (FullDuplexOut) {
        *FullDuplexOut = gRtlLinkFull;
    }
    return 0;
}
