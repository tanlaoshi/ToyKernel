/*
 * HalDevices.c — RiscV：Block / Input / Net 经 Drv 类门面（PR-D3）
 *
 * Common 只见 HalInput* / HalNet*；驱动私有头不进本文件。
 */
#include "Hal.h"
#include "Drv.h"
#include "DrvInput.h"
#include "DrvNet.h"
#include "VirtioBlk.h"
#include "VirtioInput.h"
#include "VirtioNet.h"

void HalDrvRegister(void) {
    VirtioBlkRegister();
    VirtioInputRegister();
    VirtioNetRegister();
}

int HalBlockInit(void) {
    return VirtioBlkInit();
}

int HalUsbInit(void) {
    return VirtioInputInit();
}

void HalInputPoll(void) {
    ToyDrvInputPoll();
}

int HalKeyboardDequeue(HAL_KEYBOARD_REPORT *Report) {
    return ToyDrvInputKeyboardDequeue(Report);
}

int HalKeyboardSetLeds(UINT8 Leds) {
    return ToyDrvInputKeyboardSetLeds(Leds);
}

int HalMousePresent(void) {
    return ToyDrvInputMousePresent();
}

int HalMouseDequeue(HAL_MOUSE_REPORT *Report) {
    return ToyDrvInputMouseDequeue(Report);
}

int HalNetInit(void) {
    return VirtioNetInit();
}

int HalNetReady(void) {
    return ToyDrvNetReady();
}

void HalNetPoll(void) {
    ToyDrvNetPoll();
}

void HalNetGetMac(UINT8 Mac[6]) {
    ToyDrvNetGetMac(Mac);
}

UINT32 HalNetGetIp(void) {
    return ToyDrvNetGetIp();
}

void HalNetFormatIp(UINT32 Ip, char *Buf, int BufLen) {
    ToyDrvNetFormatIp(Ip, Buf, BufLen);
}

int HalNetParseIp(const char *Text, UINT32 *Ip) {
    return ToyDrvNetParseIp(Text, Ip);
}

int HalNetPing(const char *Host, int TimeoutMs) {
    return ToyDrvNetPing(Host, TimeoutMs);
}

void HalNetGetStats(UINT32 *TxDone, UINT32 *RxFrames) {
    ToyDrvNetGetStats(TxDone, RxFrames);
}

int HalNetSendIp(UINT32 DstIp, UINT8 Proto, const void *Payload, UINTN PayloadLen) {
    return ToyDrvNetSendIp(DstIp, Proto, Payload, PayloadLen);
}

UINT16 HalNetChecksum(const void *Data, UINTN Len) {
    return ToyDrvNetChecksum(Data, Len);
}

void HalNetSetLwIpRx(int Enable) {
    ToyDrvNetSetLwIpRx(Enable);
}
