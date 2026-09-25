/*
 * EhciFtdi.c — Claim 入口 + Bulk TX/RX tee（PR-H-ehci-4）
 *
 * RX：剥 FTDI 2 字节状态前缀 → 字符环；HalSerial 轮询。
 */
#include "EhciPrivate.h"
#include "ToySerialLog.h"

#define FTDI_RX_Q   256

static char gRxQ[FTDI_RX_Q];
static UINT32 gRxR;
static UINT32 gRxW;
static int gTxBusy;

int EhciFtdiReady(void) {
    return (gEhciFtdiOk && gEhciFtdiCtrl) ? 1 : 0;
}

/* MSC hub 扫口会 Reset 子设备；作废以免 tee 打到已死 addr */
void EhciFtdiInvalidate(void) {
    gEhciFtdiOk = 0;
    gEhciFtdiCtrl = 0;
}

int EhciFtdiClaim(void) {
    int i;

    if (!gEhciReady) {
        return -1;
    }
    if (EhciFtdiReady()) {
        return 1;
    }
    ToyBootMarkUsb("Boot: EHCI FTDI claim begin\n");
    for (i = 0; i < gEhciCount; i++) {
        EHCI_CTRL *C = &gEhci[i];

        if (!C->Up || !C->Sched) {
            continue;
        }
        if (C->HubAddr && EhciFtdiClaimViaHub(C)) {
            return 1;
        }
    }
    ToyBootMarkUsb("Boot: EHCI FTDI none\n");
    return 0;
}

/* 0=ok；<0=失败（未认/忙/Bulk）。HalSerial tee 可忽略返回值。 */
int EhciFtdiWrite(const char *Text) {
    EHCI_CTRL *C = gEhciFtdiCtrl;
    UINT32 Mps;
    UINT32 N;
    UINT8 Buf[64];
    int Sent = 0;

    if (!Text || !EhciFtdiReady() || gTxBusy) {
        return -1;
    }
    gTxBusy = 1;
    Mps = gEhciFtdiMpsOut ? gEhciFtdiMpsOut : 64;
    if (Mps > sizeof(Buf)) {
        Mps = (UINT32)sizeof(Buf);
    }
    while (*Text) {
        N = 0;
        while (Text[N] && N < Mps) {
            Buf[N] = (UINT8)Text[N];
            N++;
        }
        if (EhciBulkXfer(C, gEhciFtdiAddr, gEhciFtdiEpOut, gEhciFtdiMpsOut,
                         gEhciFtdiSpeed, gEhciFtdiHubAddr, gEhciFtdiHubPort, 0,
                         Buf, N, &gEhciFtdiDtOut) < 0) {
            gTxBusy = 0;
            return -1;
        }
        Text += N;
        Sent = 1;
    }
    gTxBusy = 0;
    return Sent ? 0 : -1;
}

void EhciFtdiPollRx(void) {
    /*
     * ehci-4 验收：CoolTerm 收 tee（TX）。
     * 同步 Bulk IN 空读会堵在 WaitQtd（NAK 重试），RX 另刀再做短超时/周期 qTD。
     */
    (void)gEhciFtdiCtrl;
}

int EhciFtdiDataReady(void) {
    return (EhciFtdiReady() && gRxR != gRxW) ? 1 : 0;
}

char EhciFtdiReadChar(void) {
    char Ch;

    if (gRxR == gRxW) {
        return 0;
    }
    Ch = gRxQ[gRxR];
    gRxR = (gRxR + 1u) % FTDI_RX_Q;
    return Ch;
}
