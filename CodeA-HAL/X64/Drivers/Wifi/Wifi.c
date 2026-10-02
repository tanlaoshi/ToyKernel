/*
 * Wifi.c — 认领编排 + 状态（PR-N-wifi-1）
 *
 * 先 xHCI、再 EHCI；无棒软失败。不挂 Net。
 */
#include "WifiPrivate.h"
#include "XHCI.h"
#include "Ehci.h"
#include "ToySerialLog.h"
#include "Hal.h"

int gWifiReady;
int gWifiHost;
UINT16 gWifiPid;
UINT8 gWifiMac[6];
int gWifiFwOk;

int WifiIs8188EuPid(UINT16 Pid) {
    return Pid == WIFI_PID_8188EUS || Pid == WIFI_PID_8188ETV ||
           Pid == WIFI_PID_8188EU_A || Pid == WIFI_PID_8188EU_B;
}

void WifiLogBound(void) {
    char Line[72];
    char Hex[12];
    int n = 0;
    const char *P = "Boot: rtl8188eu ";
    const char *H;

    while (*P && n < 40) {
        Line[n++] = *P++;
    }
    H = (gWifiHost == WIFI_HOST_XHCI) ? "xhci" :
        (gWifiHost == WIFI_HOST_EHCI) ? "ehci" : "?";
    while (*H && n < 50) {
        Line[n++] = *H++;
    }
    Line[n++] = ' ';
    Line[n++] = 'p';
    Line[n++] = '=';
    HalSerialFormatHex(Hex, gWifiPid, 4);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = Hex[4];
    Line[n++] = Hex[5];
    Line[n++] = ' ';
    Line[n++] = 'f';
    Line[n++] = 'w';
    Line[n++] = '=';
    P = gWifiFwOk ? "ok" : "miss";
    while (*P && n < 68) {
        Line[n++] = *P++;
    }
    Line[n++] = '\n';
    Line[n] = 0;
    ToyLogBoot(Line);
    ToyBootMarkUsb(Line);
}

int WifiSetup(void) {
    int Rc;

    if (gWifiReady) {
        return 1;
    }
    Rc = XhciWifiClaim();
    if (Rc == 1) {
        gWifiHost = WIFI_HOST_XHCI;
        gWifiPid = XhciWifiPid();
    } else if (EhciReady()) {
        Rc = EhciWifiClaim();
        if (Rc == 1) {
            gWifiHost = WIFI_HOST_EHCI;
            gWifiPid = EhciWifiPid();
        }
    }
    if (Rc != 1) {
        return 0;
    }
    (void)WifiFwTryLoad();
    gWifiReady = 1;
    WifiLogBound();
    return 1;
}

int WifiReady(void) {
    return gWifiReady;
}

void WifiGetMac(UINT8 Mac[6]) {
    int i;

    if (!Mac) {
        return;
    }
    for (i = 0; i < 6; i++) {
        Mac[i] = gWifiReady ? gWifiMac[i] : 0;
    }
}

UINT16 WifiUsbPid(void) {
    return gWifiPid;
}

int WifiHostKind(void) {
    return gWifiReady ? gWifiHost : WIFI_HOST_NONE;
}

int WifiFwLoaded(void) {
    return gWifiFwOk;
}
