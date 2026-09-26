/*
 * Wifi.h — USB RTL8188EU 对外 API（PR-N-wifi-1）
 *
 * 探针 / 可选固件钩子；wifi-2 再挂 Net。
 */
#ifndef WIFI_H
#define WIFI_H

#include "BootTypes.h"

#define WIFI_RTL_VID       0x0BDAu
#define WIFI_PID_8188EUS   0x8179u
#define WIFI_PID_8188ETV   0x0179u
#define WIFI_PID_8188EU_A  0x817Fu
#define WIFI_PID_8188EU_B  0x818Bu

int WifiIs8188EuPid(UINT16 Pid);
int WifiSetup(void);
int WifiReady(void);
void WifiGetMac(UINT8 Mac[6]);
UINT16 WifiUsbPid(void);
/* 1=xhci 2=ehci 0=无 */
int WifiHostKind(void);
int WifiFwLoaded(void);

#endif
