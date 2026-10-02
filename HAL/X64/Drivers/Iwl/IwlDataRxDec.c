/*
 * IwlDataRxDec.c — 数据面 CCMP / 固件已解（PR-F-iwl-4）
 */
#include "IwlPrivate.h"
#include "HalSerial.h"

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

static void IwlRxLogMicFail(UINT8 *Frame, UINT8 KeyId) {
    int Us = 1;
    int K;

    for (K = 0; K < 6; K++) {
        if (Frame[4 + K] != gIwlMac[K]) {
            Us = 0;
        }
    }
    if (Us) {
        if (!gRxMicU) {
            char Line[16];
            char Hex[12];
            UINT16 FcLog;
            int N = 0;
            const char *P = "rx=u";

            gRxMicU = 1;
            FcLog = (UINT16)Frame[0] | ((UINT16)Frame[1] << 8);
            while (*P) {
                Line[N++] = *P++;
            }
            HalSerialFormatHex(Hex, FcLog, 4);
            Line[N++] = Hex[2];
            Line[N++] = Hex[3];
            Line[N++] = Hex[4];
            Line[N++] = Hex[5];
            Line[N] = 0;
            IwlLogStage(Line);
        }
    } else if (!gRxMicLogged) {
        char Line[28];
        char Hex[12];
        UINT16 FcLog;
        int N = 0;
        const char *P = "rx=mic o f=";

        gRxMicLogged = 1;
        FcLog = (UINT16)Frame[0] | ((UINT16)Frame[1] << 8);
        while (*P) {
            Line[N++] = *P++;
        }
        HalSerialFormatHex(Hex, FcLog, 4);
        Line[N++] = Hex[2];
        Line[N++] = Hex[3];
        Line[N++] = Hex[4];
        Line[N++] = Hex[5];
        Line[N++] = ' ';
        Line[N++] = 'k';
        Line[N++] = '=';
        HalSerialFormatHex(Hex, KeyId, 2);
        Line[N++] = Hex[2];
        Line[N++] = Hex[3];
        Line[N++] = ' ';
        Line[N++] = 'g';
        Line[N++] = '=';
        HalSerialFormatHex(Hex, gIwlGtkId, 2);
        Line[N++] = Hex[2];
        Line[N++] = Hex[3];
        Line[N] = 0;
        IwlLogStage(Line);
    }
}

/*
 * Prot=1 时调整 *BodyOff / *BodyLen。
 * 返回 1=可继续组以太网帧；0=丢弃。
 */
int IwlRxTryDecrypt(UINT8 *Frame, UINTN FLen, UINTN HdrLen, int FwDec,
                    UINTN *BodyOff, UINTN *BodyLen) {
    UINT64 Pn;

    /*
     * 刀 #180：固件已解（MIC_OK|DEC_DONE）→ 跳过主机 CCMP。
     * 布局仍 [mac][ccmp8][明文]；MIC 多半被 RADA 剥掉。
     */
    if (FwDec) {
        if (*BodyLen < 8u) {
            return 0;
        }
        *BodyOff = HdrLen + 8u;
        *BodyLen = FLen - HdrLen - 8u;
        if (*BodyLen >= 8u
            && !(Frame[*BodyOff] == 0xAA && Frame[*BodyOff + 1] == 0xAA)
            && *BodyLen >= 16u) {
            *BodyLen -= 8u; /* MIC 仍在 */
        }
        return 1;
    }
    if (*BodyLen < 8u + 8u) {
        return 0; /* CCMP + MIC */
    }
    /* PN + KeyID from CCMP hdr */
    Pn = (UINT64)Frame[*BodyOff]
       | ((UINT64)Frame[*BodyOff + 1] << 8)
       | ((UINT64)Frame[*BodyOff + 4] << 16)
       | ((UINT64)Frame[*BodyOff + 5] << 24)
       | ((UINT64)Frame[*BodyOff + 6] << 32)
       | ((UINT64)Frame[*BodyOff + 7] << 40);
    /*
     * 刀 #178：MAC 头长单独传给 CCMP；勿把 CCMP 算进 HdrLen（QoS AAD）。
     * Layout [mac][ccmp8][body][mic8]。
     */
    {
        UINT8 KeyId = (UINT8)((Frame[*BodyOff + 3] >> 6) & 3u);
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
            IwlRxLogMicFail(Frame, KeyId);
            return 0;
        }
        *BodyOff = HdrLen + 8u;
        *BodyLen = CryptBody;
    }
    return 1;
}
