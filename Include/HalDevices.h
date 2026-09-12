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

#define HAL_KBD_LED_NUM_LOCK     0x01
#define HAL_KBD_LED_CAPS_LOCK    0x02
#define HAL_KBD_LED_SCROLL_LOCK  0x04

typedef struct {
    UINT8 ModifierKeys;
    UINT8 Reserved;
    UINT8 KeyCode[6];
} HAL_KEYBOARD_REPORT;

/*
 * HAL_MOUSE_REPORT — 指针报告（PR-I1）
 *
 * Buttons：bit0=左、bit1=右、bit2=中（与 HID boot mouse 一致）。
 * Wheel：有符号滚轮步进（正=向上/远离用户，按 HID；无滚轮则为 0）。
 * Gui 消费滚轮见 PR-I2；本结构只负责 HAL→上层贯通。
 */
typedef struct {
    UINT32 X;
    UINT32 Y;
    UINT8  Buttons;
    INT8   Wheel;
    UINT8  Absolute; /* 1：X/Y 为 0..32767 平板坐标（QEMU usb-tablet） */
} HAL_MOUSE_REPORT;

int HalBlockInit(void);
/* PR-D2：注册平台驱动描述符（ATA / virtio-blk 等）；InitDriver 内调用 */
void HalDriverRegister(void);

int HalUsbInit(void);
/* PR-H-msc-2：MSC BringUp 壳；不自动认盘 */
int HalUsbMscInit(void);
int HalUsbMscReady(void);
/* PR-H-msc-3：Shell msc scan */
int HalUsbMscScan(void);
/* PR-H-msc-4：Shell msc claim */
int HalUsbMscClaim(void);
/* PR-H-msc-5：INQUIRY + READ CAPACITY */
int HalUsbMscCapacity(void);
UINT32 HalUsbMscBlockCount(void);
UINT32 HalUsbMscBlockSize(void);
/* 真机 PHOTO 后开 xHCI MSI-X；其它平台空操作 */
void HalInputArmIrq(void);
/* 真机：PHOTO 后再枚举鼠标，避免踩键盘 IN */
void HalInputInitMouseDeferred(void);
/* PHOTO→桌面：清空鼠队列并对齐累加坐标 */
void HalInputMouseHandoffDesktop(UINT32 CursorX, UINT32 CursorY);
void HalInputPoll(void);
/* PHOTO：t=任意xfer i=键鼠匹配 k/m推送 u未匹配 s=slot.ep c完成码 r环事件 d=Drain */
void HalInputDiagFormat(char *Buf, int Max);
int HalKeyboardDequeue(HAL_KEYBOARD_REPORT *Report);
int HalKeyboardSetLeds(UINT8 Leds);
int HalMousePresent(void);
int HalMouseDequeue(HAL_MOUSE_REPORT *Report);

int HalNetInit(void);
int HalNetReady(void);
void HalNetPoll(void);
void HalNetGetMacAddress(UINT8 Mac[6]);
UINT32 HalNetGetIpAddress(void);
void HalNetFormatIp(UINT32 Ip, char *Buf, int BufLen);
int HalNetParseIp(const char *Text, UINT32 *Ip);
int HalNetPing(const char *Host, int TimeoutMs);
void HalNetGetStats(UINT32 *TxDone, UINT32 *RxFrames);
int HalNetSendIp(UINT32 DstIp, UINT8 Proto, const void *Payload, UINTN PayloadLen);
UINT16 HalNetChecksum(const void *Data, UINTN Len);
void HalNetSetLwipReceive(int Enable);

#endif
