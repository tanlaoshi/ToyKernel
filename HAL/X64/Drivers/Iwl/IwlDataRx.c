/*
 * IwlDataRx.c — 数据面组以太网 / NetInput（PR-F-iwl-4）
 * 解密见 IwlDataRxDec.c
 */
#include "IwlPrivate.h"
#include "HalSerial.h"
#include "Net.h"

void IwlRxDataToNet(UINT8 *Frame, UINTN FLen, UINT32 St) {
    UINT16 Fc;
    UINTN HdrLen;
    UINTN BodyOff;
    UINTN BodyLen;
    UINT8 Eth[1518];
    UINTN EthLen;
    UINTN I;
    int Prot;
    int Qos;
    int FromDs;
    int ToDs;
    int FwDec;

    if (!Frame || FLen < 24) {
        return;
    }
    Fc = (UINT16)Frame[0] | ((UINT16)Frame[1] << 8);
    if (((Fc >> 2) & 0x3u) != 0x2u) {
        return; /* 非 data */
    }
    Prot = (Fc & 0x4000u) != 0;
    Qos = ((Fc & 0x008Cu) == 0x0088u);
    ToDs = (Fc & 0x0100u) != 0;
    FromDs = (Fc & 0x0200u) != 0;
    FwDec = (St & IWL_RX_MPDU_MIC_OK) != 0 && (St & IWL_RX_MPDU_DEC_DONE) != 0;
    HdrLen = 24u;
    if ((Fc & 0x0300u) == 0x0300u) {
        HdrLen += 6u;
    }
    if (Qos) {
        HdrLen += 2u;
        if ((Fc & 0x8000u) != 0) {
            HdrLen += 4u; /* HT Control */
        }
    }
    if (FLen < HdrLen + 8u) {
        return;
    }
    BodyOff = HdrLen;
    BodyLen = FLen - HdrLen;
    if (Prot) {
        if (!IwlRxTryDecrypt(Frame, FLen, HdrLen, FwDec, &BodyOff, &BodyLen)) {
            return;
        }
    }
    if (BodyLen < 8u) {
        return;
    }
    /* LLC/SNAP */
    if (Frame[BodyOff] != 0xAA || Frame[BodyOff + 1] != 0xAA) {
        if (!gRxLlcLogged) {
            gRxLlcLogged = 1;
            IwlLogStage("rx=llc");
        }
        return;
    }
    /* DA / SA */
    if (FromDs && !ToDs) {
        for (I = 0; I < 6; I++) {
            Eth[I] = Frame[4 + I];      /* Addr1 DA */
            Eth[6 + I] = Frame[16 + I]; /* Addr3 SA */
        }
    } else if (ToDs && !FromDs) {
        for (I = 0; I < 6; I++) {
            Eth[I] = Frame[16 + I];
            Eth[6 + I] = Frame[10 + I];
        }
    } else {
        return;
    }
    Eth[12] = Frame[BodyOff + 6];
    Eth[13] = Frame[BodyOff + 7];
    /* 0x888E 是 EAPOL，握手已经结束，不要送进 lwIP，也不要占掉第一帧记录 */
    if (Eth[12] == 0x88 && Eth[13] == 0x8E) {
        return;
    }
    EthLen = 14u + (BodyLen - 8u);
    if (EthLen > sizeof(Eth)) {
        /* 曾 Eth[640]：catalog HTTP 响应 ~840B → 静默丢 → store http empty */
        IwlLogStage("eth>mtu");
        return;
    }
    for (I = 0; I < BodyLen - 8u; I++) {
        Eth[14 + I] = Frame[BodyOff + 8 + I];
    }
    if (Eth[12] == 0x08 && Eth[13] == 0x00 && EthLen >= 38u && Eth[23] == 17u) {
        UINT16 Sp = (UINT16)(((UINT16)Eth[34] << 8) | Eth[35]);
        UINT16 Dp = (UINT16)(((UINT16)Eth[36] << 8) | Eth[37]);

        if (Sp == 0x0044u && Dp == 0x0043u) {
            if (!gRxDiscLogged) {
                gRxDiscLogged = 1;
                IwlLogStage("rx=disc");
            }
        } else if (Sp == 0x0043u && Dp == 0x0044u) {
            if (!gRxOffLogged) {
                gRxOffLogged = 1;
                IwlLogStage("rx=off");
            }
        } else if (!gDatRxLogged) {
            gDatRxLogged = 1;
            IwlLogStage("rx=0800");
        }
    } else if (!gDatRxLogged) {
        gDatRxLogged = 1;
        IwlLogStage("rx=oth");
    }
    NetInputFrame(Eth, EthLen);
}
