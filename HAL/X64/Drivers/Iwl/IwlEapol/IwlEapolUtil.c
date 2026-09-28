/*
 * IwlEapolUtil.c — WPA2-PSK 四次握手辅助（PR-S-iwl-split-1，自 IwlEapol.c 搬家）
 */
#include "IwlPrivate.h"
#include "HalSerial.h"

void IwlEapolZero(void *P, UINTN N) {
    UINT8 *B = (UINT8 *)P;
    UINTN i;
    for (i = 0; i < N; i++) {
        B[i] = 0;
    }
}

void IwlEapolCopyN(UINT8 *D, const UINT8 *S, UINTN N) {
    UINTN i;
    for (i = 0; i < N; i++) {
        D[i] = S[i];
    }
}

int IwlEapolMemCmp(const UINT8 *A, const UINT8 *B, UINTN N) {
    UINTN i;
    for (i = 0; i < N; i++) {
        if (A[i] != B[i]) {
            return (int)A[i] - (int)B[i];
        }
    }
    return 0;
}

UINTN IwlDot11DataHdrLen(UINT16 Fc) {
    UINTN HdrLen = 24u;

    if ((Fc & 0x0300u) == 0x0300u) {
        HdrLen += 6u; /* Addr4 */
    }
    if ((Fc & 0x008Cu) == 0x0088u) {
        HdrLen += 2u; /* QoS */
        if (Fc & 0x8000u) {
            HdrLen += 4u; /* Order → HT Control */
        }
    }
    if (Fc & 0x4000u) {
        HdrLen += 8u; /* CCMP/TKIP IV（明文 EAPOL 无此位） */
    }
    return HdrLen;
}

/*
 * 在 data 帧里找 SNAP 88 8E → EAPOL。
 * 刀 #116：Order/HT、头后最多扫 32B（防对齐/垫片）；返回 1=找到。
 */
int IwlFindEapol(UINT8 *Dot11, UINTN FLen, UINT8 **OutEap, UINTN *OutLen) {
    UINT16 Fc;
    UINTN HdrLen;
    UINTN Off;
    UINTN End;
    UINTN BodyLen;
    UINT16 Elen;

    if (!Dot11 || FLen < 32) {
        return 0;
    }
    Fc = (UINT16)Dot11[0] | ((UINT16)Dot11[1] << 8);
    if (((Fc >> 2) & 0x3u) != 0x2u) {
        return 0;
    }
    /* QoS Null / Null：无载荷 */
    if ((Fc & 0x00FCu) == 0x00C8u || (Fc & 0x00FCu) == 0x0048u) {
        return 0;
    }
    HdrLen = IwlDot11DataHdrLen(Fc);
    if (FLen < HdrLen + 8u + 4u) {
        return 0;
    }
    End = HdrLen + 32u;
    if (End + 8u > FLen) {
        End = FLen - 8u;
    }
    for (Off = HdrLen; Off <= End; Off++) {
        if (Dot11[Off] != 0xAA || Dot11[Off + 1] != 0xAA) {
            continue;
        }
        if (Dot11[Off + 6] != 0x88 || Dot11[Off + 7] != 0x8E) {
            continue;
        }
        Off += 8;
        BodyLen = FLen - Off;
        if (BodyLen < 4u || Dot11[Off + 1] != 0x03) {
            return 0;
        }
        Elen = (UINT16)(((UINT16)Dot11[Off + 2] << 8) | Dot11[Off + 3]);
        if ((UINTN)(4u + Elen) > BodyLen) {
            return 0;
        }
        if ((UINTN)(4u + Elen) < 99u) {
            return 0; /* Start/其它短帧；msg1 至少 99 */
        }
        *OutEap = Dot11 + Off;
        *OutLen = 4u + (UINTN)Elen;
        return 1;
    }
    return 0;
}

int IwlAddr1IsUs(const UINT8 *Dot11) {
    UINTN i;

    for (i = 0; i < 6; i++) {
        if (Dot11[4 + i] != gIwlMac[i]) {
            return 0;
        }
    }
    return 1;
}
UINT16 IwlBe16(const UINT8 *P) {
    return (UINT16)(((UINT16)P[0] << 8) | P[1]);
}

void IwlPutBe16(UINT8 *P, UINT16 V) {
    P[0] = (UINT8)(V >> 8);
    P[1] = (UINT8)V;
}
