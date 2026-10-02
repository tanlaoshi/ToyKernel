/*
 * IwlEapolHand.c — WPA2-PSK 四次握手辅助（PR-S-iwl-split-1，自 IwlEapol.c 搬家）
 */
#include "IwlPrivate.h"
#include "HalSerial.h"

int IwlEapolFinishHandshake(UINT8 *Eapol, UINTN EapLenIn, UINT8 *Anonce, UINT8 *Replay,
                            UINT8 KeyDesc) {
    UINT8 Pmk[32];
    UINT8 Ptk[48];
    UINT8 Kck[16];
    UINT8 Snonce[32];
    UINT32 i;
    int Got3 = 0;
    UINTN SsidLen;
    UINT16 KeyInfo;
    UINT8 KeyVer = 2;
    UINT8 EapVer = 1;
    UINTN EapLen = EapLenIn;

    IwlEapolZero(Pmk, 32);
    IwlEapolZero(Ptk, 48);
    IwlEapolZero(Kck, 16);
    IwlEapolZero(Snonce, 32);

    {
        char Line[20];
        char Hex[12];
        int n = 0;
        const char *P = "wpa2=m1 k=";
        KeyInfo = IwlBe16(Eapol + 5);
        while (*P) {
            Line[n++] = *P++;
        }
        HalSerialFormatHex(Hex, (KeyInfo >> 8) & 0xffu, 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        HalSerialFormatHex(Hex, KeyInfo & 0xffu, 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        Line[n] = 0;
        IwlLogStage(Line);
        KeyVer = (UINT8)(KeyInfo & 7u);
        if (KeyVer != 3) {
            KeyVer = 2;
        }
        EapVer = Eapol[0];
        if (EapVer < 1 || EapVer > 2) {
            EapVer = 1;
        }
    }

    if (gIwlPmkOk) {
        IwlEapolCopyN(Pmk, gIwlPmk, 32);
    } else {
        SsidLen = 0;
        while (gIwlSsid[SsidLen]) {
            SsidLen++;
        }
        if (!IwlPbkdf2Sha1(gIwlPsk, (const UINT8 *)gIwlSsid, SsidLen, 4096, Pmk, 32)) {
            IwlLogStage("wpa2=pmk");
            return 0;
        }
    }

    for (i = 0; i < 32; i++) {
        Snonce[i] = (UINT8)(gIwlMac[i % 6] ^ (UINT8)(i * 17u + 3u));
    }
    IwlBuildPtk(Pmk, Anonce, Snonce, Ptk, KeyVer);
    IwlEapolCopyN(Kck, Ptk, 16);
    IwlEapolCopyN(gIwlPtk, Ptk + 32, 16);

    /*
     * 刀 #146：M2 曾塞 beacon RSN。刀 #173 后 AssocReq 已是自建 PSK RSN；
     * 刀 #174：M2 Key Data 必须同一份（gIwlStaRsn），否则 Apple 空口 ACK 但不回 M3。
     * 刀 #147：KeyInfo 版本跟 M1（2=HMAC-SHA1，3=AES-CMAC）。
     */
    IwlEapolZero(Eapol, 256u);
    Eapol[0] = EapVer;
    Eapol[1] = 3;
    Eapol[4] = KeyDesc;
    IwlPutBe16(Eapol + 5, (UINT16)(0x0108u | KeyVer));
    IwlPutBe16(Eapol + 7, 16);
    IwlEapolCopyN(Eapol + 9, Replay, 8);
    IwlEapolCopyN(Eapol + 17, Snonce, 32);
    {
        UINTN Total = 99;
        UINT8 M2[160];
        UINT8 Kd = 0;
        int UsedSta = 0;
        UINT32 Eap3 = 0;
        int SawKi = 0;
        int MicLogged = 0;

        if (gIwlStaRsnLen >= 4 && gIwlStaRsnLen <= 32 &&
            99u + (UINTN)gIwlStaRsnLen <= 256u) {
            Kd = gIwlStaRsnLen;
            IwlEapolCopyN(Eapol + 99, gIwlStaRsn, Kd);
            UsedSta = 1;
        } else if (gIwlTarget.RsnLen >= 4 && gIwlTarget.RsnLen <= 48 &&
                   99u + (UINTN)gIwlTarget.RsnLen <= 256u) {
            Kd = gIwlTarget.RsnLen;
            IwlEapolCopyN(Eapol + 99, gIwlTarget.Rsn, Kd);
        }
        if (Kd) {
            IwlPutBe16(Eapol + 97, Kd);
            IwlPutBe16(Eapol + 2, (UINT16)(95u + Kd));
            Total = 99u + Kd;
        } else {
            IwlPutBe16(Eapol + 97, 0);
            IwlPutBe16(Eapol + 2, 95);
        }
        {
            UINT8 Mic[16];
            IwlEapolMic(KeyVer, Kck, Eapol, Total, Mic);
            (void)Mic;
        }
        if (IwlSendEapol(Eapol, Total) != 0) {
            IwlLogStage("wpa2=m2tx");
            return 0;
        }
        IwlEapolCopyN(M2, Eapol, Total);
        if (KeyVer == 3 && Kd) {
            IwlLogStage("wpa2=m2c");
        } else if (UsedSta) {
            IwlLogStage("wpa2=m2s");
        } else {
            IwlLogStage(Kd ? "wpa2=m2r" : "wpa2=m2");
        }

        gIwlTxRsp = 0;
        gIwlRxCode = 0;
        {
            UINT32 RxMpdu = 0;
            UINT32 RxData = 0;
            UINT32 RxUni = 0;
            UINT32 RxBeacon = 0;

            for (i = 0; i < 4000 && !Got3; i++) {
                if (i == 1000u || i == 2000u) {
                    (void)IwlSendEapol(M2, Total);
                }
                IwlRxPoll();
                while (!Got3 && IwlRxDrainForEapol(Eapol, &EapLen, 256u,
                                                   &RxMpdu, &RxData, &RxUni,
                                                   &RxBeacon)) {
                    UINT8 Calc[16];
                    UINT8 Saved[16];
                    UINTN k;
                    int Diff;

                    Eap3++;
                    if (!SawKi) {
                        char Line[28];
                        char Hex[12];
                        int n = 0;
                        const char *P = "wpa2=ki d=";
                        UINT16 Ki = IwlBe16(Eapol + 5);
                        while (*P) {
                            Line[n++] = *P++;
                        }
                        HalSerialFormatHex(Hex, Eapol[4], 2);
                        Line[n++] = Hex[2];
                        Line[n++] = Hex[3];
                        Line[n++] = ' ';
                        Line[n++] = 'k';
                        Line[n++] = '=';
                        HalSerialFormatHex(Hex, (Ki >> 8) & 0xffu, 2);
                        Line[n++] = Hex[2];
                        Line[n++] = Hex[3];
                        HalSerialFormatHex(Hex, Ki & 0xffu, 2);
                        Line[n++] = Hex[2];
                        Line[n++] = Hex[3];
                        Line[n] = 0;
                        IwlLogStage(Line);
                        SawKi = 1;
                    }
                    if (Eapol[4] != 2 && Eapol[4] != 254) {
                        continue;
                    }
                    KeyInfo = IwlBe16(Eapol + 5);
                    /* M1 重传只有 ACK、没有 MIC。MIC 位置位就验，不再要求 ACK。 */
                    if ((KeyInfo & 0x0100u) == 0) {
                        continue;
                    }
                    IwlEapolCopyN(Saved, Eapol + 81, 16);
                    IwlEapolMic(KeyVer, Kck, Eapol, EapLen, Calc);
                    Diff = 0;
                    for (k = 0; k < 16; k++) {
                        Diff |= (int)(Calc[k] ^ Saved[k]);
                    }
                    if (Diff != 0) {
                        if (!MicLogged) {
                            char Line[20];
                            char Hex[12];
                            int n = 0;
                            const char *P = "wpa2=mic k=";
                            while (*P) {
                                Line[n++] = *P++;
                            }
                            HalSerialFormatHex(Hex, (KeyInfo >> 8) & 0xffu, 2);
                            Line[n++] = Hex[2];
                            Line[n++] = Hex[3];
                            HalSerialFormatHex(Hex, KeyInfo & 0xffu, 2);
                            Line[n++] = Hex[2];
                            Line[n++] = Hex[3];
                            Line[n] = 0;
                            IwlLogStage(Line);
                            MicLogged = 1;
                        }
                        continue;
                    }
                    IwlEapolCopyN(Replay, Eapol + 9, 8);
                    Got3 = 1;
                }
                if (!Got3) {
                    IwlStallMs(1);
                }
            }
            if (!Got3) {
                char Line[48];
                char Hex[12];
                int n = 0;
                const char *P = "wpa2=m3to e=";
                if (!gIwlTxRsp) {
                    IwlLogApQ();
                    IwlLogStage("txa=none");
                }
                while (*P) {
                    Line[n++] = *P++;
                }
                HalSerialFormatHex(Hex, Eap3 & 0xffu, 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                Line[n++] = ' ';
                Line[n++] = 'n';
                Line[n++] = '=';
                HalSerialFormatHex(Hex, RxMpdu & 0xffu, 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                Line[n++] = ' ';
                Line[n++] = 'd';
                Line[n++] = '=';
                HalSerialFormatHex(Hex, RxData & 0xffu, 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                Line[n++] = ' ';
                Line[n++] = 'u';
                Line[n++] = '=';
                HalSerialFormatHex(Hex, RxUni & 0xffu, 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                Line[n++] = ' ';
                Line[n++] = 'b';
                Line[n++] = '=';
                HalSerialFormatHex(Hex, RxBeacon & 0xffu, 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                Line[n] = 0;
                IwlLogStage(Line);
                return 0;
            }
        }
    }
    IwlLogStage("wpa2=m3");
    IwlInstallGtk(Eapol, EapLen, Ptk + 16);

    IwlEapolZero(Eapol, 256u);
    Eapol[0] = EapVer;
    Eapol[1] = 3;
    IwlPutBe16(Eapol + 2, 95);
    Eapol[4] = KeyDesc;
    IwlPutBe16(Eapol + 5, (UINT16)(0x0308u | KeyVer));
    IwlPutBe16(Eapol + 7, 16);
    IwlEapolCopyN(Eapol + 9, Replay, 8);
    IwlPutBe16(Eapol + 97, 0);
    {
        UINT8 Mic[16];
        IwlEapolMic(KeyVer, Kck, Eapol, 99, Mic);
        (void)Mic;
    }
    if (IwlSendEapol(Eapol, 99) != 0) {
        IwlLogStage("wpa2=m4tx");
        return 0;
    }

    gIwlWpa2Ok = 1;
    IwlLogStage("wpa2=ok");
    /* 刀 #180：握手完再装固件钥；失败软退，主机 CCMP 仍可试 */
    if (!IwlStaKeysInstall()) {
        IwlLogStage("key=soft");
    }
    return 1;
}
