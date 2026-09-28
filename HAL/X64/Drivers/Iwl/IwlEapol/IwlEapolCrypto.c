/*
 * IwlEapolCrypto.c — WPA2-PSK 四次握手辅助（PR-S-iwl-split-1，自 IwlEapol.c 搬家）
 */
#include "IwlPrivate.h"
#include "HalSerial.h"

void IwlPrf384(const UINT8 Pmk[32], const UINT8 *A, UINTN Alen,
                     const UINT8 *B, UINTN Blen, UINT8 Out[48]) {
    UINT8 Buf[128];
    UINT8 Tmp[20];
    UINTN Pos = 0;
    UINT8 Count = 0;
    UINTN OutPos = 0;
    UINTN i;

    for (i = 0; i < Alen && Pos < sizeof(Buf); i++) {
        Buf[Pos++] = A[i];
    }
    if (Pos < sizeof(Buf)) {
        Buf[Pos++] = 0;
    }
    for (i = 0; i < Blen && Pos < sizeof(Buf); i++) {
        Buf[Pos++] = B[i];
    }
    while (OutPos < 48) {
        UINT8 Input[160];
        UINTN InLen = 0;
        for (i = 0; i < Pos; i++) {
            Input[InLen++] = Buf[i];
        }
        Input[InLen++] = Count;
        IwlHmacSha1(Pmk, 32, Input, InLen, Tmp);
        for (i = 0; i < 20 && OutPos < 48; i++) {
            Out[OutPos++] = Tmp[i];
        }
        Count++;
    }
}

void IwlBuildPtk(const UINT8 Pmk[32], const UINT8 *Anon, const UINT8 *Snon,
                        UINT8 Ptk[48], UINT8 Ver) {
    UINT8 A[22];
    UINT8 B[76];
    UINTN i;
    const char *Lab = "Pairwise key expansion";
    const UINT8 *MinMac;
    const UINT8 *MaxMac;
    const UINT8 *MinNon;
    const UINT8 *MaxNon;

    for (i = 0; i < 22; i++) {
        A[i] = (UINT8)Lab[i];
    }
    if (IwlEapolMemCmp(gIwlMac, gIwlBssid, 6) < 0) {
        MinMac = gIwlMac;
        MaxMac = gIwlBssid;
    } else {
        MinMac = gIwlBssid;
        MaxMac = gIwlMac;
    }
    if (IwlEapolMemCmp(Anon, Snon, 32) < 0) {
        MinNon = Anon;
        MaxNon = Snon;
    } else {
        MinNon = Snon;
        MaxNon = Anon;
    }
    IwlEapolCopyN(B, MinMac, 6);
    IwlEapolCopyN(B + 6, MaxMac, 6);
    IwlEapolCopyN(B + 12, MinNon, 32);
    IwlEapolCopyN(B + 44, MaxNon, 32);
    if (Ver == 3) {
        IwlKdfSha256(Pmk, 32, Lab, B, 76, Ptk, 48);
    } else {
        IwlPrf384(Pmk, A, 22, B, 76, Ptk);
    }
}

/* EAPOL-Key MIC：版本 2 = HMAC-SHA1-128；版本 3 = AES-128-CMAC */
void IwlEapolMic(UINT8 Ver, const UINT8 Kck[16], UINT8 *Eapol,
                        UINTN EapolLen, UINT8 Mic[16]) {
    UINT8 Dig[32];
    UINTN i;
    for (i = 0; i < 16; i++) {
        Eapol[81 + i] = 0;
        Dig[i] = 0;
    }
    if (Ver == 3) {
        IwlAesCmac(Kck, Eapol, EapolLen, Dig);
    } else {
        IwlHmacSha1(Kck, 16, Eapol, EapolLen, Dig);
    }
    for (i = 0; i < 16; i++) {
        Mic[i] = Dig[i];
        Eapol[81 + i] = Dig[i];
    }
}

void IwlInstallGtk(const UINT8 *Eapol, UINTN EapLen, const UINT8 Kek[16]) {
    UINT16 Kd;
    UINT8 Plain[192];
    UINTN PlainLen = 0;
    UINTN i;

    gIwlGtkAltOk = 0;
    if (EapLen < 101u) {
        IwlLogStage("gtk=no");
        return;
    }
    Kd = (UINT16)(((UINT16)Eapol[97] << 8) | Eapol[98]);
    if (Kd < 16u || 99u + (UINTN)Kd > EapLen) {
        IwlLogStage("gtk=no");
        return;
    }
    {
        char Line[16];
        char Hex[12];
        int n = 0;
        const char *P = "gtk=kd=";
        while (*P) {
            Line[n++] = *P++;
        }
        HalSerialFormatHex(Hex, Kd & 0xffu, 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        Line[n] = 0;
        IwlLogStage(Line);
    }
    if (!IwlAesUnwrap(Kek, Eapol + 99, Kd, Plain, sizeof(Plain), &PlainLen)) {
        IwlLogStage("gtk=bad");
        return;
    }
    for (i = 0; i + 2u <= PlainLen; ) {
        UINT8 Id = Plain[i];
        UINT8 KdeLen = Plain[i + 1];

        if (i + 2u + (UINTN)KdeLen > PlainLen) {
            break;
        }
        if (Id == 0xddu && (UINTN)KdeLen >= 6u + 16u
            && Plain[i + 2] == 0x00u && Plain[i + 3] == 0x0fu
            && Plain[i + 4] == 0xacu && Plain[i + 5] == 0x01u) {
            /* DD Len | 00-0F-AC-01 | KeyID | Rsvd | GTK[16+]（802.11：域长=Len-6） */
            gIwlGtkId = (UINT8)(Plain[i + 6] & 3u);
            IwlEapolCopyN(gIwlGtk, Plain + i + 8, 16);
            if ((UINTN)KdeLen >= 6u + 16u + 8u
                && i + 16u + 16u <= i + 2u + (UINTN)KdeLen) {
                IwlEapolCopyN(gIwlGtkAlt, Plain + i + 16, 16);
                gIwlGtkAltOk = 1;
            }
            {
                char Line[24];
                char Hex[12];
                int n = 0;
                const char *P = "gtk=ln=";
                while (*P) {
                    Line[n++] = *P++;
                }
                HalSerialFormatHex(Hex, KdeLen, 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                Line[n++] = ' ';
                Line[n++] = 'i';
                Line[n++] = 'd';
                Line[n++] = '=';
                HalSerialFormatHex(Hex, gIwlGtkId, 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                Line[n++] = ' ';
                Line[n++] = 'o';
                Line[n++] = '=';
                Line[n++] = '0';
                Line[n++] = '8';
                if (gIwlGtkAltOk) {
                    Line[n++] = '+';
                }
                Line[n] = 0;
                IwlLogStage(Line);
            }
            IwlLogStage("gtk=ok");
            return;
        }
        i += 2u + (UINTN)KdeLen;
    }
    IwlLogStage("gtk=no");
}
