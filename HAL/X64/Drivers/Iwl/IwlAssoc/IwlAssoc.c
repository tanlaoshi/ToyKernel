/*
 * IwlAssoc.c — 关联入口（PR-S-iwl-split-4）
 */
#include "IwlAssocInternal.h"
#include "HalSerial.h"

UINT8 gIwlStaRsn[32];
UINT8 gIwlStaRsnLen;

int IwlAssocRun(void) {
    UINT8 Frame[160];
    UINT8 Pay[120];
    UINTN Flen = 0;
    UINTN i;
    IWL_RX_PKT *Pkt;
    UINTN Len;
    int GotAssoc = 0;
    int Attempt;
    UINT32 RxN = 0;
    UINT8 FirstFc = 0;

    if (!gIwlSsidOk) {
        return 0;
    }
    IwlAssocCopyN(gIwlBssid, gIwlTarget.Bssid, 6);
    gIwlAssociated = 0;
    gIwlAid = 0;
    gIwlStaRsnLen = 0;

    (void)IwlPhyCtxtTune(gIwlTarget.Chan);

    {
        int EverAuth = 0;

        for (Attempt = 0; Attempt < 4 && !GotAssoc; Attempt++) {
            RxN = 0;
            FirstFc = 0;

            if (!EverAuth) {
                Pay[0] = 0;
                Pay[1] = 0;
                Pay[2] = 1;
                Pay[3] = 0;
                Pay[4] = 0;
                Pay[5] = 0;
                IwlBuildMgmt(Frame, &Flen, 0xB, Pay, 6);
                if (IwlSendFrameRaw(Frame, Flen) != 0) {
                    return 0;
                }
                if (Attempt == 0) {
                    (void)IwlSendFrameRaw(Frame, Flen);
                }
                if (!IwlAssocWaitAuth(&RxN, &FirstFc)) {
                    continue;
                }
                IwlLogStage("auth=ok");
                EverAuth = 1;
            } else {
                IwlLogVerb("auth=keep");
            }

            Pay[0] = (UINT8)(gIwlTarget.Caps);
            Pay[1] = (UINT8)(gIwlTarget.Caps >> 8);
            Pay[2] = 0x0A;
            Pay[3] = 0x00;
            Pay[4] = 0;
            Pay[5] = gIwlTarget.SsidLen;
            IwlAssocCopyN(Pay + 6, gIwlTarget.Ssid, gIwlTarget.SsidLen);
            {
                UINTN P = 6 + gIwlTarget.SsidLen;
                UINT8 RatesLen = gIwlTarget.RatesLen;
                UINT8 ExtLen = gIwlTarget.ExtRatesLen;
                static const UINT8 DefRates[] = {
                    0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24
                };
                static const UINT8 DefExt[] = {
                    0x30, 0x48, 0x60, 0x6c
                };

                if (RatesLen == 0) {
                    RatesLen = (UINT8)sizeof(DefRates);
                    IwlAssocCopyN(Pay + P + 2, DefRates, RatesLen);
                } else {
                    IwlAssocCopyN(Pay + P + 2, gIwlTarget.Rates, RatesLen);
                }
                Pay[P] = 1;
                Pay[P + 1] = RatesLen;
                P += 2 + RatesLen;

                if (ExtLen == 0 && gIwlTarget.RatesLen == 0) {
                    ExtLen = (UINT8)sizeof(DefExt);
                    IwlAssocCopyN(Pay + P + 2, DefExt, ExtLen);
                    Pay[P] = 50;
                    Pay[P + 1] = ExtLen;
                    P += 2 + ExtLen;
                } else if (ExtLen > 0) {
                    IwlAssocCopyN(Pay + P + 2, gIwlTarget.ExtRates, ExtLen);
                    Pay[P] = 50;
                    Pay[P + 1] = ExtLen;
                    P += 2 + ExtLen;
                }

                if (gIwlTarget.HtLen > 0 && P + 2 + gIwlTarget.HtLen <= sizeof(Pay)) {
                    if (Attempt == 0) {
                        Pay[P] = 45;
                        Pay[P + 1] = gIwlTarget.HtLen;
                        IwlAssocCopyN(Pay + P + 2, gIwlTarget.Ht, gIwlTarget.HtLen);
                        P += 2 + gIwlTarget.HtLen;
                    }
                }
                if (gIwlTarget.HasRsn && gIwlPsk[0]) {
                    UINTN Rl = IwlBuildStaRsn(Pay + P, sizeof(Pay) - P);
                    if (Rl == 0) {
                        IwlLogStage("assoc=norsn");
                        return 0;
                    }
                    P += Rl;
                }
                IwlBuildMgmt(Frame, &Flen, 0x0, Pay, P);
            }
            if (Attempt == 0) {
                char Ie[20];
                int en = 0;
                const char *Ep = "assoc=ie h=";
                while (*Ep) {
                    Ie[en++] = *Ep++;
                }
                Ie[en++] = gIwlTarget.HtLen ? '1' : '0';
                Ie[en++] = ' ';
                Ie[en++] = 'r';
                Ie[en++] = '=';
                Ie[en++] = (gIwlTarget.RsnLen >= 4) ? '1' : '0';
                Ie[en] = 0;
                IwlLogStage(Ie);
            }
            if (IwlSendFrameRaw(Frame, Flen) != 0) {
                IwlLogStage("assoc=txfail");
                return 0;
            }
            IwlLogStage("assoc=tx");
            if (Attempt == 0) {
                (void)IwlSendFrameRaw(Frame, Flen);
            }
            IwlLogStage("assoc=wait");
            RxN = 0;
            FirstFc = 0;
            if (!IwlAssocWaitResp(&RxN, &FirstFc, &GotAssoc)) {
                if (Attempt < 3) {
                    IwlLogStage("assoc=retry");
                }
                continue;
            }
        }
    }
    if (!GotAssoc) {
        IwlLogStage("assoc=fail");
        return 0;
    }
    gIwlAssociated = 1;
    IwlLogStage("assoc=ok");
    if (!IwlMacCtxtPrep()) {
        IwlLogStage("prep=soft");
    }
    IwlLogStage("drain=go");
    for (i = 0; i < 80; i++) {
        int Took = 0;

        IwlRxPoll();
        while (Took < 32 && IwlAssocTake(&Pkt, &Len)) {
            const UINT8 *Dot;
            UINTN PayLen;

            Took++;
            if (Pkt->Hdr.Code != IWL_RX_MPDU_CMD || Len < 12) {
                continue;
            }
            Dot = Pkt->Data + 4;
            PayLen = Len - sizeof(IWL_CMD_HDR) - 4;
            if (PayLen >= 24 && ((Dot[0] >> 2) & 0x3u) == 0x2u) {
                IwlRxHoldMpdu(Pkt, Len);
            }
        }
        IwlStallMs(1);
    }
    return 1;
}
