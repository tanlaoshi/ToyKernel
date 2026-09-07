/*
 * HalDevices.c — Arm64：Block / Input / Net 经 Driver 类门面（PR-D3）
 *
 * Common 只见 HalInput* / HalNet*；驱动私有头不进本文件。
 */
#include "Hal.h"
#include "Driver.h"
#include "DriverInput.h"
#include "DriverNet.h"
#include "VirtioBlk.h"
#include "VirtioInput.h"
#include "VirtioNet.h"

void HalDriverRegister(void) {
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
    ToyDriverInputPoll();
}

int HalKeyboardDequeue(HAL_KEYBOARD_REPORT *Report) {
    return ToyDriverInputKeyboardDequeue(Report);
}

int HalKeyboardSetLeds(UINT8 Leds) {
    return ToyDriverInputKeyboardSetLeds(Leds);
}

int HalMousePresent(void) {
    return ToyDriverInputMousePresent();
}

int HalMouseDequeue(HAL_MOUSE_REPORT *Report) {
    return ToyDriverInputMouseDequeue(Report);
}

int HalNetInit(void) {
    return VirtioNetInit();
}

int HalNetReady(void) {
    return ToyDriverNetReady();
}

void HalNetPoll(void) {
    ToyDriverNetPoll();
}

void HalNetGetMacAddress(UINT8 Mac[6]) {
    ToyDriverNetGetMac(Mac);
}

UINT32 HalNetGetIpAddress(void) {
    return ToyDriverNetGetIp();
}

void HalNetFormatIp(UINT32 Ip, char *Buf, int BufLen) {
    ToyDriverNetFormatIp(Ip, Buf, BufLen);
}

int HalNetParseIp(const char *Text, UINT32 *Ip) {
    return ToyDriverNetParseIp(Text, Ip);
}

int HalNetPing(const char *Host, int TimeoutMs) {
    return ToyDriverNetPing(Host, TimeoutMs);
}

void HalNetGetStats(UINT32 *TxDone, UINT32 *RxFrames) {
    ToyDriverNetGetStats(TxDone, RxFrames);
}

int HalNetSendIp(UINT32 DstIp, UINT8 Proto, const void *Payload, UINTN PayloadLen) {
    return ToyDriverNetSendIp(DstIp, Proto, Payload, PayloadLen);
}

UINT16 HalNetChecksum(const void *Data, UINTN Len) {
    return ToyDriverNetChecksum(Data, Len);
}

void HalNetSetLwipReceive(int Enable) {
    ToyDriverNetSetLwIpRx(Enable);
}
