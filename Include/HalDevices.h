/*
 * HalDevices.h — 块设备 / USB 输入 / 网卡 HAL 门面（Common 经 Hal.h 使用）
 */
#ifndef HAL_DEVICES_H
#define HAL_DEVICES_H

#include "BootTypes.h"

#define HAL_NET_IP_DEFAULT  0x0A00020FULL
#define HAL_IP_PROTO_ICMP   1
#define HAL_IP_PROTO_TCP    6
#define HAL_IP_PROTO_UDP    17

typedef struct {
    UINT8 ModifierKeys;
    UINT8 Reserved;
    UINT8 KeyCode[6];
} HAL_KEYBOARD_REPORT;

typedef struct {
    UINT32 X;
    UINT32 Y;
    UINT8  Buttons;
} HAL_MOUSE_REPORT;

int HalBlockInit(void);

int HalUsbInit(void);
void HalInputPoll(void);
int HalKeyboardDequeue(HAL_KEYBOARD_REPORT *Report);
int HalMousePresent(void);
int HalMouseDequeue(HAL_MOUSE_REPORT *Report);

int HalNetInit(void);
int HalNetReady(void);
void HalNetPoll(void);
void HalNetGetMac(UINT8 Mac[6]);
UINT32 HalNetGetIp(void);
void HalNetFormatIp(UINT32 Ip, char *Buf, int BufLen);
int HalNetParseIp(const char *Text, UINT32 *Ip);
int HalNetPing(const char *Host, int TimeoutMs);
void HalNetGetStats(UINT32 *TxDone, UINT32 *RxFrames);
int HalNetSendIp(UINT32 DstIp, UINT8 Proto, const void *Payload, UINTN PayloadLen);
UINT16 HalNetChecksum(const void *Data, UINTN Len);

#endif
