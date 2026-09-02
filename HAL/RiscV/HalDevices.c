/*
 * HalDevices.c — RISC-V 设备 HAL 占位
 */
#include "Hal.h"

int HalBlockInit(void) {
    return -1;
}

int HalUsbInit(void) {
    return -1;
}

void HalInputPoll(void) {
}

int HalKeyboardDequeue(HAL_KEYBOARD_REPORT *Report) {
    (void)Report;
    return 0;
}

int HalKeyboardSetLeds(UINT8 Leds) {
    (void)Leds;
    return -1;
}

int HalMousePresent(void) {
    return 0;
}

int HalMouseDequeue(HAL_MOUSE_REPORT *Report) {
    (void)Report;
    return 0;
}

int HalNetInit(void) {
    return -1;
}

int HalNetReady(void) {
    return 0;
}

void HalNetPoll(void) {
}

void HalNetGetMac(UINT8 Mac[6]) {
    int i;
    for (i = 0; i < 6; i++) {
        Mac[i] = 0;
    }
}

UINT32 HalNetGetIp(void) {
    return 0;
}

void HalNetFormatIp(UINT32 Ip, char *Buf, int BufLen) {
    (void)Ip;
    if (BufLen > 0) {
        Buf[0] = 0;
    }
}

int HalNetParseIp(const char *Text, UINT32 *Ip) {
    (void)Text;
    (void)Ip;
    return -1;
}

int HalNetPing(const char *Host, int TimeoutMs) {
    (void)Host;
    (void)TimeoutMs;
    return -1;
}

void HalNetGetStats(UINT32 *TxDone, UINT32 *RxFrames) {
    if (TxDone) {
        *TxDone = 0;
    }
    if (RxFrames) {
        *RxFrames = 0;
    }
}

int HalNetSendIp(UINT32 DstIp, UINT8 Proto, const void *Payload, UINTN PayloadLen) {
    (void)DstIp;
    (void)Proto;
    (void)Payload;
    (void)PayloadLen;
    return -1;
}

UINT16 HalNetChecksum(const void *Data, UINTN Len) {
    (void)Data;
    (void)Len;
    return 0;
}

void HalNetSetLwIpRx(int Enable) {
    (void)Enable;
}
