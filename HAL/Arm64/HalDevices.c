/*
 * HalDevices.c — Arm64：virtio-blk + virtio-input + virtio-net（PR-V3/V4/N9）
 */
#include "Hal.h"
#include "VirtioBlk.h"
#include "VirtioInput.h"
#include "VirtioNet.h"

int HalBlockInit(void) {
    return VirtioBlkInit();
}

int HalUsbInit(void) {
    return VirtioInputInit();
}

void HalInputPoll(void) {
    VirtioInputPoll();
}

int HalKeyboardDequeue(HAL_KEYBOARD_REPORT *Report) {
    return VirtioInputKeyboardDequeue(Report);
}

int HalKeyboardSetLeds(UINT8 Leds) {
    (void)Leds;
    return -1;
}

int HalMousePresent(void) {
    return VirtioInputMousePresent();
}

int HalMouseDequeue(HAL_MOUSE_REPORT *Report) {
    return VirtioInputMouseDequeue(Report);
}

int HalNetInit(void) {
    return VirtioNetInit();
}

int HalNetReady(void) {
    return VirtioNetReady();
}

void HalNetPoll(void) {
    VirtioNetPoll();
}

void HalNetGetMac(UINT8 Mac[6]) {
    VirtioNetGetMac(Mac);
}

UINT32 HalNetGetIp(void) {
    return VirtioNetGetIp();
}

void HalNetFormatIp(UINT32 Ip, char *Buf, int BufLen) {
    VirtioNetFormatIp(Ip, Buf, BufLen);
}

int HalNetParseIp(const char *Text, UINT32 *Ip) {
    return VirtioNetParseIp(Text, Ip);
}

int HalNetPing(const char *Host, int TimeoutMs) {
    return VirtioNetPing(Host, TimeoutMs);
}

void HalNetGetStats(UINT32 *TxDone, UINT32 *RxFrames) {
    VirtioNetGetStats(TxDone, RxFrames);
}

int HalNetSendIp(UINT32 DstIp, UINT8 Proto, const void *Payload, UINTN PayloadLen) {
    return VirtioNetSendIp(DstIp, Proto, Payload, PayloadLen);
}

UINT16 HalNetChecksum(const void *Data, UINTN Len) {
    return VirtioNetChecksum(Data, Len);
}

void HalNetSetLwIpRx(int Enable) {
    VirtioNetSetLwIpRx(Enable);
}
