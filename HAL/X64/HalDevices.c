/*
 * HalDevices.c — x86：Input / Net 经 Drv 类门面（PR-D3）
 *
 * Common 只见 HalInput* / HalNet*；本文件不 include XHCI/Net 私有实现细节以外的
 * 注册入口头（仍经 Drivers/ 薄包装注册）。
 */
#include "Hal.h"
#include "DrvInput.h"
#include "DrvNet.h"
#include "InputXhci.h"
#include "Net.h"

/* BlockAta.c / BlockAhci.c（PR-H1：AHCI 为第二 Block 后端） */
void AtaDrvRegister(void);
void AhciDrvRegister(void);

void HalDrvRegister(void) {
    /* AHCI 先注册；VMM 后 HalBlockInit 再 Probe 时可覆盖 ATA 后端 */
    AhciDrvRegister();
    AtaDrvRegister();
    InputXhciRegister();
    NetDrvRegister();
}

int HalUsbInit(void) {
    return InputXhciInit();
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
    return NetInit();
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
