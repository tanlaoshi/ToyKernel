/*
 * IwlAssocRsn.c — STA RSN IE（PR-S-iwl-split-4）
 */
#include "IwlAssocInternal.h"
#include "HalSerial.h"

UINTN IwlBuildStaRsn(UINT8 *Out, UINTN Cap) {
    const UINT8 *R = gIwlTarget.Rsn;
    UINT8 Rl = gIwlTarget.RsnLen;
    UINTN Off;
    UINT16 PairCnt;
    UINT16 AkmCnt;
    UINT16 Caps = 0;
    int HasPsk = 0;
    int HasSae = 0;
    UINTN i;
    UINT8 Group[4];

    Group[0] = 0x00;
    Group[1] = 0x0f;
    Group[2] = 0xac;
    Group[3] = 0x04; /* CCMP 默认 */
    if (Rl >= 8 && R[0] == 48) {
        /*
         * 刀 #183 强制 CCMP 组播 → AP 不发 M1（m1to）。
         * 刀 #184：组播仍跟 beacon（常 TKIP=02）；DHCP 改单播走 PTK/CCMP。
         */
        {
            char Line[20];
            char Hex[12];
            int n = 0;
            const char *P = "assoc=gc=";
            while (*P) {
                Line[n++] = *P++;
            }
            HalSerialFormatHex(Hex, R[7], 2);
            Line[n++] = Hex[2];
            Line[n++] = Hex[3];
            Line[n] = 0;
            IwlLogStage(Line);
        }
        Group[0] = R[4];
        Group[1] = R[5];
        Group[2] = R[6];
        Group[3] = R[7];
        gIwlGroupCipher = R[7]; /* 2=TKIP 4=CCMP；组播解密选型 */
        Off = 8;
        if (Off + 2 <= Rl) {
            PairCnt = (UINT16)R[Off] | ((UINT16)R[Off + 1] << 8);
            Off = Off + 2u + (UINTN)PairCnt * 4u;
        }
        if (Off + 2 <= Rl) {
            AkmCnt = (UINT16)R[Off] | ((UINT16)R[Off + 1] << 8);
            Off += 2;
            for (i = 0; i < AkmCnt && Off + 4u <= Rl; i++) {
                if (R[Off] == 0x00 && R[Off + 1] == 0x0f && R[Off + 2] == 0xac) {
                    if (R[Off + 3] == 0x02) {
                        HasPsk = 1;
                    }
                    if (R[Off + 3] == 0x08) {
                        HasSae = 1;
                    }
                }
                Off += 4;
            }
            if (Off + 2u <= Rl) {
                Caps = (UINT16)R[Off] | ((UINT16)R[Off + 1] << 8);
            }
        }
    } else {
        HasPsk = 1; /* 无可用 beacon RSN 时按旧静态 IE */
    }

    {
        char Line[28];
        char Hex[12];
        int n = 0;
        const char *P = "assoc=rsn p=";

        while (*P) {
            Line[n++] = *P++;
        }
        Line[n++] = HasPsk ? '1' : '0';
        Line[n++] = ' ';
        Line[n++] = 's';
        Line[n++] = '=';
        Line[n++] = HasSae ? '1' : '0';
        Line[n++] = ' ';
        Line[n++] = 'c';
        Line[n++] = '=';
        HalSerialFormatHex(Hex, (Caps >> 8) & 0xffu, 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        HalSerialFormatHex(Hex, Caps & 0xffu, 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        Line[n] = 0;
        IwlLogStage(Line);
    }

    if (!HasPsk) {
        IwlLogStage("assoc=wpa3");
        return 0;
    }

    /* STA RSN：只报 PSK；MFPC 跟随 AP，清除 MFPR（本驱动不做 802.11w） */
    if (Cap < 22u) {
        return 0;
    }
    Caps &= (UINT16)~(1u << 6); /* MFPR off */
    Out[0] = 48;
    Out[1] = 20;
    Out[2] = 0x01;
    Out[3] = 0x00;
    Out[4] = Group[0];
    Out[5] = Group[1];
    Out[6] = Group[2];
    Out[7] = Group[3];
    Out[8] = 0x01;
    Out[9] = 0x00;
    Out[10] = 0x00;
    Out[11] = 0x0f;
    Out[12] = 0xac;
    Out[13] = 0x04;
    Out[14] = 0x01;
    Out[15] = 0x00;
    Out[16] = 0x00;
    Out[17] = 0x0f;
    Out[18] = 0xac;
    Out[19] = 0x02;
    Out[20] = (UINT8)(Caps & 0xffu);
    Out[21] = (UINT8)((Caps >> 8) & 0xffu);
    /* 刀 #174：缓存给 M2，避免再塞 beacon 整段 */
    IwlAssocCopyN(gIwlStaRsn, Out, 22);
    gIwlStaRsnLen = 22;
    return 22;
}

