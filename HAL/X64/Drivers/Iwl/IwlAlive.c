/*
 * IwlAlive.c — 等 ALIVE；解析 scd_base_ptr；排空 RX
 */
#include "IwlPrivate.h"
#include "HalSerial.h"
#include "ToySerialLog.h"

UINT32 gIwlSchedBase;

static void IwlAppend(char *Line, int *N, int Max, const char *S) {
    while (*S && *N < Max) {
        Line[(*N)++] = *S++;
    }
}

static void IwlDumpSbCpu(void) {
    char Line[100];
    char Hex[20];
    UINT32 A = 0x5a5a5a5au;
    UINT32 B = 0x5a5a5a5au;
    int n = 0;

    if (IwlNicLock()) {
        A = IwlPrphR(IWL_SB_CPU_1_STATUS);
        B = IwlPrphR(IWL_SB_CPU_2_STATUS);
        IwlNicUnlock();
    }
    IwlAppend(Line, &n, 90, "Boot: iwl8265 sb_cpu1=");
    HalSerialFormatHex(Hex, A, 8);
    IwlAppend(Line, &n, 90, Hex + 2);
    IwlAppend(Line, &n, 90, " cpu2=");
    HalSerialFormatHex(Hex, B, 8);
    IwlAppend(Line, &n, 90, Hex + 2);
    Line[n++] = '\n';
    Line[n] = 0;
    ToyLogBoot(Line);
}

/* ALIVE payload：v1/v2/v3 的 scd_base_ptr 都在 offset 40 */
static void IwlParseAlivePayload(const UINT8 *P, UINTN PayLen) {
    UINT32 Scd = 0;

    if (PayLen < 44) {
        return;
    }
    Scd = P[40] | ((UINT32)P[41] << 8) | ((UINT32)P[42] << 16)
        | ((UINT32)P[43] << 24);
    if (Scd != 0) {
        gIwlSchedBase = Scd;
    }
}

void IwlRxDrain(void) {
    IWL_RX_PKT *Pkt;
    UINTN Len;
    UINT32 Guard = 0;

    while (IwlRxTake(&Pkt, &Len) && Guard < 512) {
        if (Pkt->Hdr.Code == IWL_ALIVE) {
            UINTN Pay = Len > 4 + sizeof(IWL_CMD_HDR)
                      ? Len - 4 - sizeof(IWL_CMD_HDR) : 0;
            if (Pay) {
                IwlParseAlivePayload(Pkt->Data, Pay);
            }
        }
        Guard++;
    }
}

int IwlWaitAlive(void) {
    UINT32 i;
    IWL_RX_PKT *Pkt;
    UINTN Len;
    int GotCsr = 0;

    for (i = 0; i < 8000; i++) {
        UINT32 Int = IwlMmioR32(IWL_CSR_INT);
        if (Int) {
            IwlMmioW32(IWL_CSR_INT, Int);
            if (Int & IWL_CSR_INT_ALIVE) {
                GotCsr = 1;
                gIwlAlive = 1;
            }
            if (Int & IWL_CSR_INT_FH_RX) {
                IwlMmioW32(IWL_CSR_FH_INT_STATUS, 0x00FFFFFFu);
            }
        }
        while (IwlRxTake(&Pkt, &Len)) {
            if (Pkt->Hdr.Code == IWL_ALIVE) {
                UINTN Pay = Len > 4 + sizeof(IWL_CMD_HDR)
                          ? Len - 4 - sizeof(IWL_CMD_HDR) : 0;
                UINT16 Status = 0;
                if (Pay >= 2) {
                    Status = Pkt->Data[0] | ((UINT16)Pkt->Data[1] << 8);
                }
                if (Pay) {
                    IwlParseAlivePayload(Pkt->Data, Pay);
                }
                if (Status == IWL_ALIVE_OK || Status == 0 || GotCsr) {
                    gIwlAlive = 1;
                    /* 再吸干净同批通知 */
                    IwlRxDrain();
                    return 1;
                }
            }
        }
        if (GotCsr) {
            /* CSR 已到：再等片刻吸 RX 里的 ALIVE 解析 scd_base */
            IwlStallMs(20);
            IwlRxDrain();
            gIwlAlive = 1;
            return 1;
        }
        IwlStallMs(1);
    }
    IwlDumpSbCpu();
    return 0;
}
