/*
 * Iwl.c — Setup / 状态 / 黄字（PR-N-wifi-1）
 */
#include "IwlPrivate.h"
#include "Hal.h"
#include "HalSerial.h"
#include "ToySerialLog.h"

int gIwlReady;
int gIwlFwOk;
UINT16 gIwlDid;
UINT8 gIwlBus;
UINT8 gIwlDev;
UINT8 gIwlFn;
UINT8 gIwlMac[6];

void IwlLogBound(void) {
    char Line[80];
    char Hex[12];
    int n = 0;
    const char *P = "Boot: iwl8265 ";
    const char *F;

    while (*P && n < 40) {
        Line[n++] = *P++;
    }
    Line[n++] = 'b';
    Line[n++] = '=';
    HalSerialFormatHex(Hex, gIwlBus, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = ':';
    HalSerialFormatHex(Hex, gIwlDev, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = '.';
    HalSerialFormatHex(Hex, gIwlFn, 1);
    Line[n++] = Hex[2];
    Line[n++] = ' ';
    Line[n++] = 'f';
    Line[n++] = 'w';
    Line[n++] = '=';
    F = gIwlFwOk ? "ok" : "miss";
    while (*F && n < 76) {
        Line[n++] = *F++;
    }
    Line[n++] = '\n';
    Line[n] = 0;
    ToyLogBoot(Line); /* 只走 Boot 通道，避免 Boot+Net 双写串口 */
}

int IwlSetup(void) {
    UINT8 Bus;
    UINT8 Dev;
    UINT8 Fn;
    UINT16 Did = 0;
    int HadFw;

    if (gIwlReady) {
        /* 已认卡：仅在先前 fw=miss 时再试一次，状态变化才再打黄字 */
        if (!gIwlFwOk) {
            HadFw = 0;
            (void)IwlFwTryLoad();
            if (gIwlFwOk && !HadFw) {
                IwlLogBound();
            }
        }
        return 1;
    }
    if (!IwlPciFind(&Bus, &Dev, &Fn, &Did)) {
        return 0;
    }
    gIwlBus = Bus;
    gIwlDev = Dev;
    gIwlFn = Fn;
    gIwlDid = Did;
    (void)IwlFwTryLoad();
    gIwlReady = 1;
    IwlLogBound();
    return 1;
}

int IwlReady(void) {
    return gIwlReady;
}

void IwlGetMac(UINT8 Mac[6]) {
    int i;

    if (!Mac) {
        return;
    }
    for (i = 0; i < 6; i++) {
        Mac[i] = gIwlReady ? gIwlMac[i] : 0;
    }
}

UINT16 IwlPciDid(void) {
    return gIwlDid;
}

int IwlFwLoaded(void) {
    return gIwlFwOk;
}
