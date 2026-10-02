/*
 * IwlEapolM1Poll.c — M1 等待 RX 抽帧（PR-F-iwl-1）
 */
#include "IwlEapolInternal.h"

int IwlEapolM1DrainRx(IWL_EAPOL_M1_CTX *C, UINT8 *Eapol, UINTN *EapLen,
                      UINT8 *Anonce, UINT8 *Replay, UINT32 LoopI) {
    int Took = 0;

    for (;;) {
        IWL_RX_PKT *Pkt;
        UINTN Len;
        UINT8 Mutable[512];
        UINTN FLen;
        UINT8 *Ep;
        UINTN EpLen;
        UINTN K;
        UINT16 Fc;
        int ToUs;

        if (Took >= 24) {
            break;
        }
        Took++;
        if (IwlRxTakeHeld(&Pkt, &Len)) {
        } else if (!IwlRxTake(&Pkt, &Len)) {
            break;
        }
        if (Pkt->Hdr.Code != IWL_RX_MPDU_CMD || Len < 8) {
            continue;
        }
        C->RxMpdu++;
        FLen = Len - sizeof(IWL_CMD_HDR) - 4;
        if (FLen > sizeof(Mutable)) {
            FLen = sizeof(Mutable);
        }
        for (K = 0; K < FLen; K++) {
            Mutable[K] = Pkt->Data[4 + K];
        }
        if (FLen < 24) {
            continue;
        }
        Fc = (UINT16)Mutable[0] | ((UINT16)Mutable[1] << 8);
        if (C->FirstFc0 == 0 && C->FirstFc1 == 0) {
            C->FirstFc0 = Mutable[0];
            C->FirstFc1 = Mutable[1];
        }
        /* mgmt deauth/disassoc：AP 已踢掉则无需空等 M1 */
        if (((Fc >> 2) & 0x3u) == 0u) {
            UINT8 Sub = (UINT8)((Fc >> 4) & 0x0fu);
            if (Sub == 12u || Sub == 10u) {
                IwlLogStage(Sub == 12u ? "rx=deauth" : "rx=disassoc");
                C->Got1 = 0;
                return 1;
            }
            continue;
        }
        if (((Fc >> 2) & 0x3u) != 0x2u) {
            continue;
        }
        C->RxData++;
        ToUs = IwlAddr1IsUs(Mutable);
        if (!C->DataDaOk) {
            for (K = 0; K < 6; K++) {
                C->DataDa[K] = Mutable[4 + K];
            }
            C->DataDaOk = 1;
        }
        if (ToUs) {
            C->RxUni++;
            if (!C->UniSnapOk) {
                UINTN H = IwlDot11DataHdrLen(Fc);
                C->UniFc0 = Mutable[0];
                C->UniFc1 = Mutable[1];
                for (K = 0; K < 8 && H + K < FLen; K++) {
                    C->UniSnap[K] = Mutable[H + K];
                }
                C->UniSnapOk = 1;
            }
        }
        if (IwlFindEapol(Mutable, FLen, &Ep, &EpLen) && EpLen >= 99
            && EpLen <= 256u) {
            C->EapHit++;
            if (Ep[4] == 2 || Ep[4] == 254) {
                C->KeyInfo = IwlBe16(Ep + 5);
                C->LastKi = C->KeyInfo;
                /* M1：Ack、无 MIC；Accept Install=0 或偶发脏位 */
                if ((C->KeyInfo & 0x0080u) != 0 && (C->KeyInfo & 0x0100u) == 0) {
                    int Newer = 0;
                    if (!C->Got1) {
                        Newer = 1;
                    } else {
                        for (K = 0; K < 8; K++) {
                            if (Ep[9 + K] > Replay[K]) {
                                Newer = 1;
                                break;
                            }
                            if (Ep[9 + K] < Replay[K]) {
                                break;
                            }
                        }
                    }
                    if (Newer) {
                        IwlEapolCopyN(Eapol, Ep, EpLen);
                        *EapLen = EpLen;
                        IwlEapolCopyN(Anonce, Ep + 17, 32);
                        IwlEapolCopyN(Replay, Ep + 9, 8);
                        C->KeyDesc = Ep[4];
                        C->Got1 = 1;
                        C->M1At = LoopI;
                        if (C->M1Fresh) {
                            IwlLogStage("wpa2=m1new");
                        }
                        C->M1Fresh = 1;
                    }
                }
            } else {
                C->LastKi = (UINT16)Ep[4];
            }
        }
    }
    return 0;
}
