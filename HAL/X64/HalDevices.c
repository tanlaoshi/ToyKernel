/*
 * HalDevices.c — x86：Input / Net 经 Driver 类门面（PR-D3）
 *
 * Common 只见 HalInput* / HalNet*；本文件不 include XHCI/Net 私有实现细节以外的
 * 注册入口头（仍经 Drivers/ 薄包装注册）。
 */
#include "Hal.h"
#include "DriverInput.h"
#include "DriverNet.h"
#include "InputXhci.h"
#include "Net.h"

/* BlockAta.c / BlockAhci.c（PR-H1：AHCI 为第二 Block 后端） */
void AtaDriverRegister(void);
void AhciDriverRegister(void);

void HalDriverRegister(void) {
    /* AHCI 先注册；VMM 后 HalBlockInit 再 Probe 时可覆盖 ATA 后端 */
    AhciDriverRegister();
    AtaDriverRegister();
    InputXhciRegister();
    NetDriverRegister();
}

int HalUsbInit(void) {
    return InputXhciInit();
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
    return NetInit();
}

int HalNetReady(void) {
    return ToyDriverNetReady();
}

void HalNetPoll(void) {
    ToyDriverNetPoll();
}

void HalNetGetMac(UINT8 Mac[6]) {
    ToyDriverNetGetMac(Mac);
}

UINT32 HalNetGetIp(void) {
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

void HalNetSetLwIpRx(int Enable) {
    ToyDriverNetSetLwIpRx(Enable);
}
