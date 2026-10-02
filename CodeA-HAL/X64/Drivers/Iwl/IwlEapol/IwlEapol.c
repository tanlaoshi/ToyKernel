/*
 * IwlEapol.c — WPA2-PSK 四次握手入口（PR-N-wifi-2；PR-S-iwl-split-1 瘦身）
 *
 * 刀 #85：先收 msg1 再 PBKDF2。GTK 解包略。永不串口打印 PSK。
 */
#include "IwlPrivate.h"
#include "HalSerial.h"

int IwlEapolRun(void) {
    UINT8 Eapol[256];
    UINTN EapLen = 0;
    UINT8 Anonce[32];
    UINT8 Replay[8];
    UINT8 KeyDesc = 2;

    gIwlWpa2Ok = 0;
    IwlEapolZero(Anonce, 32);
    IwlEapolZero(Replay, 8);
    IwlEapolZero(Eapol, sizeof(Eapol));

    if (!gIwlAssociated || !gIwlPsk[0] || !gIwlSsid[0]) {
        IwlLogStage("wpa2=nocfg");
        return 0;
    }
    if (!IwlEapolWaitMsg1(Eapol, &EapLen, Anonce, Replay, &KeyDesc)) {
        return 0;
    }
    return IwlEapolFinishHandshake(Eapol, EapLen, Anonce, Replay, KeyDesc);
}
