/*
 * IwlSend.c — 以太网→802.11 发送（PR-S-iwl-split-2，自 Iwl.c 搬家）
 */
#include "IwlPrivate.h"
#include "HalSerial.h"

int IwlSendFrame(const UINT8 *Frame, UINTN FrameLen) {
    /* 以太网 → 802.11 ToDS data + LLC；WPA2 时插 CCMP 头并加密 */
    UINT8 Wlan[420];
    UINTN i;
    UINTN BodyLen;
    UINTN WireLen;
    UINT64 Pn;
    static UINT64 gTxPn = 1;

    if (!IwlAssociated() || !Frame || FrameLen < 14 || FrameLen > 360) {
        return -1;
    }
    BodyLen = 8u + (FrameLen - 14u); /* LLC/SNAP + payload */
    /* FC data ToDS；WPA2 置 Protected */
    Wlan[0] = 0x08;
    Wlan[1] = (UINT8)(gIwlWpa2Ok ? 0x41u : 0x01u); /* ToDS | Protected? */
    Wlan[2] = 0;
    Wlan[3] = 0;
    for (i = 0; i < 6; i++) {
        Wlan[4 + i] = gIwlBssid[i];
    }
    for (i = 0; i < 6; i++) {
        Wlan[10 + i] = Frame[6 + i]; /* SA */
    }
    for (i = 0; i < 6; i++) {
        Wlan[16 + i] = Frame[i]; /* DA */
    }
    Wlan[22] = 0;
    Wlan[23] = 0;

    if (gIwlWpa2Ok) {
        /*
         * 队列 4 是用 tid 8 打开的，EAPOL 因此能被取走。
         * tid 6 的 QoS 帧停在环上。数据改回非 QoS，tid 仍由发送函数写成 8。
         */
        Pn = gTxPn++;
        Wlan[24] = (UINT8)Pn;
        Wlan[25] = (UINT8)(Pn >> 8);
        Wlan[26] = 0;
        Wlan[27] = 0x20; /* ExtIV，KeyID=0 */
        Wlan[28] = (UINT8)(Pn >> 16);
        Wlan[29] = (UINT8)(Pn >> 24);
        Wlan[30] = (UINT8)(Pn >> 32);
        Wlan[31] = (UINT8)(Pn >> 40);
        Wlan[32] = 0xAA;
        Wlan[33] = 0xAA;
        Wlan[34] = 0x03;
        Wlan[35] = 0;
        Wlan[36] = 0;
        Wlan[37] = 0;
        Wlan[38] = Frame[12];
        Wlan[39] = Frame[13];
        for (i = 0; i < FrameLen - 14; i++) {
            Wlan[40 + i] = Frame[14 + i];
        }
        /*
         * 明文交给固件加密。主机先算的 MIC 能通过自检，
         * 接入点只确认了 FCS，没有发回 Offer。
         */
        WireLen = 32u + BodyLen;
    } else {
        Wlan[24] = 0xAA;
        Wlan[25] = 0xAA;
        Wlan[26] = 0x03;
        Wlan[27] = 0;
        Wlan[28] = 0;
        Wlan[29] = 0;
        Wlan[30] = Frame[12];
        Wlan[31] = Frame[13];
        for (i = 0; i < FrameLen - 14; i++) {
            Wlan[32 + i] = Frame[14 + i];
        }
        WireLen = 24u + BodyLen;
    }
    if (IwlSendFrameRaw(Wlan, WireLen) != 0) {
        return -1;
    }
    if (!gDatTxLogged) {
        gDatTxLogged = 1;
        IwlLogStage("dat=fw");
    }
    return 0;
}
