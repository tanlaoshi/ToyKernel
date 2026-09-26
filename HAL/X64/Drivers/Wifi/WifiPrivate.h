/*
 * WifiPrivate.h — wifi-1 内部
 */
#ifndef WIFI_PRIVATE_H
#define WIFI_PRIVATE_H

#include "Wifi.h"

#define WIFI_HOST_NONE  0
#define WIFI_HOST_XHCI  1
#define WIFI_HOST_EHCI  2

#define WIFI_FW_PATH_A  "FW/RTL8188EU.BIN"
#define WIFI_FW_PATH_B  "rtl8188eu.bin"

extern int gWifiReady;
extern int gWifiHost;
extern UINT16 gWifiPid;
extern UINT8 gWifiMac[6];
extern int gWifiFwOk;

int WifiFwTryLoad(void);
void WifiLogBound(void);

#endif
