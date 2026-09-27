/*
 * IwlMac.c — 刀 #133/#134：WFMP 网卡地址。
 * 读到后先黄字；扫描完成再写入 gIwlMac。
 */
#include "IwlPrivate.h"
#include "HalSerial.h"

#define IWL_WFMP_MAC_ADDR_0 0xA03080u
#define IWL_WFMP_MAC_ADDR_1 0xA03084u

static UINT8 gIwlHwMac[6];
static int gIwlHwMacOk;

static int IwlMacPlausible(const UINT8 Mac[6]) {
    int i;
    int Any = 0;

    if ((Mac[0] & 1u) != 0) {
        return 0;
    }
    if (Mac[0] == 0xA5u && Mac[1] == 0xA5u && Mac[2] == 0xA5u) {
        return 0;
    }
    if (Mac[0] == 0xFFu && Mac[1] == 0xFFu && Mac[2] == 0xFFu) {
        return 0;
    }
    for (i = 0; i < 6; i++) {
        if (Mac[i] != 0) {
            Any = 1;
        }
    }
    return Any;
}

void IwlReadHwMac(void) {
    UINT32 A0;
    UINT32 A1;
    UINT8 Mac[6];
    char Line[24];
    char Hex[12];
    int n;
    int i;
    const char *P = "mac=";

    if (!IwlNicLock()) {
        IwlLogStage("mac=keep");
        return;
    }
    A0 = IwlPrphR(IWL_WFMP_MAC_ADDR_0);
    A1 = IwlPrphR(IWL_WFMP_MAC_ADDR_1);
    IwlNicUnlock();
    if (A0 == 0xA5A5A5A5u || A0 == 0xA5A5A5A2u ||
        A1 == 0xA5A5A5A5u || A1 == 0xA5A5A5A2u) {
        IwlLogStage("mac=keep");
        return;
    }
    /* iwl_flip_hw_address：ADDR0 四字节倒序，ADDR1 低 16 位按字交换 */
    Mac[0] = (UINT8)(A0 >> 24);
    Mac[1] = (UINT8)(A0 >> 16);
    Mac[2] = (UINT8)(A0 >> 8);
    Mac[3] = (UINT8)A0;
    Mac[4] = (UINT8)(A1 >> 8);
    Mac[5] = (UINT8)A1;
    if (!IwlMacPlausible(Mac)) {
        IwlLogStage("mac=keep");
        return;
    }
    for (i = 0; i < 6; i++) {
        gIwlHwMac[i] = Mac[i];
    }
    gIwlHwMacOk = 1;
    n = 0;
    while (*P && n < 16) {
        Line[n++] = *P++;
    }
    for (i = 0; i < 6; i++) {
        HalSerialFormatHex(Hex, Mac[i], 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
    }
    Line[n] = 0;
    IwlLogStage(Line);
}

void IwlApplyHwMac(void) {
    int i;

    if (!gIwlHwMacOk) {
        return;
    }
    for (i = 0; i < 6; i++) {
        gIwlMac[i] = gIwlHwMac[i];
    }
    IwlLogStage("mac=use");
}
