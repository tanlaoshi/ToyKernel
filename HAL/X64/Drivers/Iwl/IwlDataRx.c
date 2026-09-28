/*
 * IwlDataRx.c — 数据面解密与 NetInput（PR-S-iwl-split-2，自 Iwl.c 搬家）
 */
#include "IwlPrivate.h"
#include "HalSerial.h"
#include "Net.h"

/* KeyID≠0 优先 GTK（组播 Offer）；KeyID=0 优先 PTK；刀 #182 再试 GtkAlt */
static int IwlDecryptEither(UINT8 *Frame, UINT64 Pn, UINTN MacHdr,
                            UINTN CryptBody, UINT8 KeyId) {
    if (KeyId != 0) {
        if (IwlCcmpDecrypt(gIwlGtk, Pn, Frame, MacHdr, CryptBody)) {
            return 1;
        }
        if (gIwlGtkAltOk
            && IwlCcmpDecrypt(gIwlGtkAlt, Pn, Frame, MacHdr, CryptBody)) {
            if (!gRxMicLogged) {
                IwlLogStage("gtk=o16");
            }
            return 1;
        }
        return IwlCcmpDecrypt(gIwlPtk, Pn, Frame, MacHdr, CryptBody);
    }
    if (IwlCcmpDecrypt(gIwlPtk, Pn, Frame, MacHdr, CryptBody)) {
        return 1;
    }
    if (IwlCcmpDecrypt(gIwlGtk, Pn, Frame, MacHdr, CryptBody)) {
        return 1;
    }
    if (gIwlGtkAltOk
        && IwlCcmpDecrypt(gIwlGtkAlt, Pn, Frame, MacHdr, CryptBody)) {
        return 1;
    }
    return 0;
}

void IwlRxDataToNet(UINT8 *Frame, UINTN FLen, UINT32 St) {
    UINT16 Fc;
    UINTN HdrLen;
    UINTN BodyOff;
    UINTN BodyLen;
    UINT8 Eth[640];
    UINTN EthLen;
    UINTN i;
    UINT64 Pn;
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
        /*
         * 刀 #180：固件已解（MIC_OK|DEC_DONE）→ 跳过主机 CCMP。
         * 布局仍 [mac][ccmp8][明文]；MIC 多半被 RADA 剥掉。
         */
        if (FwDec) {
            if (BodyLen < 8u) {
                return;
            }
            BodyOff = HdrLen + 8u;
            BodyLen = FLen - HdrLen - 8u;
            if (BodyLen >= 8u
                && !(Frame[BodyOff] == 0xAA && Frame[BodyOff + 1] == 0xAA)
                && BodyLen >= 16u) {
                BodyLen -= 8u; /* MIC 仍在 */
            }
        } else {
        if (BodyLen < 8u + 8u) {
            return; /* CCMP + MIC */
        }
        /* PN + KeyID from CCMP hdr */
        Pn = (UINT64)Frame[BodyOff]
           | ((UINT64)Frame[BodyOff + 1] << 8)
           | ((UINT64)Frame[BodyOff + 4] << 16)
           | ((UINT64)Frame[BodyOff + 5] << 24)
           | ((UINT64)Frame[BodyOff + 6] << 32)
           | ((UINT64)Frame[BodyOff + 7] << 40);
        /*
         * 刀 #178：MAC 头长单独传给 CCMP；勿把 CCMP 算进 HdrLen（QoS AAD）。
         * Layout [mac][ccmp8][body][mic8]。
         */
        {
            UINT8 KeyId = (UINT8)((Frame[BodyOff + 3] >> 6) & 3u);
            UINTN CryptBody = FLen - HdrLen - 8u - 8u;
            int Ok = IwlDecryptEither(Frame, Pn, HdrLen, CryptBody, KeyId);

            /* 刀 #179：byte_count 可能含 FCS(4)；再试 -4/-8 */
            if (!Ok && CryptBody > 4u) {
                Ok = IwlDecryptEither(Frame, Pn, HdrLen, CryptBody - 4u, KeyId);
                if (Ok) {
                    CryptBody -= 4u;
                }
            }
            if (!Ok && CryptBody > 8u) {
                Ok = IwlDecryptEither(Frame, Pn, HdrLen, CryptBody - 8u, KeyId);
                if (Ok) {
                    CryptBody -= 8u;
                }
            }
            if (!Ok) {
                {
                    int Us = 1;
                    int k;

                    for (k = 0; k < 6; k++) {
                        if (Frame[4 + k] != gIwlMac[k]) {
                            Us = 0;
                        }
                    }
                    if (Us) {
                        if (!gRxMicU) {
                            char Line[16];
                            char Hex[12];
                            UINT16 FcLog;
                            int n = 0;
                            const char *P = "rx=u";

                            gRxMicU = 1;
                            FcLog = (UINT16)Frame[0] | ((UINT16)Frame[1] << 8);
                            while (*P) {
                                Line[n++] = *P++;
                            }
                            HalSerialFormatHex(Hex, FcLog, 4);
                            Line[n++] = Hex[2];
                            Line[n++] = Hex[3];
                            Line[n++] = Hex[4];
                            Line[n++] = Hex[5];
                            Line[n] = 0;
                            IwlLogStage(Line);
                        }
                    } else if (!gRxMicLogged) {
                        char Line[28];
                        char Hex[12];
                        UINT16 FcLog;
                        int n = 0;
                        const char *P = "rx=mic o f=";

                        gRxMicLogged = 1;
                        FcLog = (UINT16)Frame[0] | ((UINT16)Frame[1] << 8);
                        while (*P) {
                            Line[n++] = *P++;
                        }
                        HalSerialFormatHex(Hex, FcLog, 4);
                        Line[n++] = Hex[2];
                        Line[n++] = Hex[3];
                        Line[n++] = Hex[4];
                        Line[n++] = Hex[5];
                        Line[n++] = ' ';
                        Line[n++] = 'k';
                        Line[n++] = '=';
                        HalSerialFormatHex(Hex, KeyId, 2);
                        Line[n++] = Hex[2];
                        Line[n++] = Hex[3];
                        Line[n++] = ' ';
                        Line[n++] = 'g';
                        Line[n++] = '=';
                        HalSerialFormatHex(Hex, gIwlGtkId, 2);
                        Line[n++] = Hex[2];
                        Line[n++] = Hex[3];
                        Line[n] = 0;
                        IwlLogStage(Line);
                    }
                }
                return;
            }
            BodyOff = HdrLen + 8u;
            BodyLen = CryptBody;
        }
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
        for (i = 0; i < 6; i++) {
            Eth[i] = Frame[4 + i];      /* Addr1 DA */
            Eth[6 + i] = Frame[16 + i]; /* Addr3 SA */
        }
    } else if (ToDs && !FromDs) {
        for (i = 0; i < 6; i++) {
            Eth[i] = Frame[16 + i];
            Eth[6 + i] = Frame[10 + i];
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
        return;
    }
    for (i = 0; i < BodyLen - 8u; i++) {
        Eth[14 + i] = Frame[BodyOff + 8 + i];
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
