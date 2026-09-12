/*
 * HalDevices.c — RiscV：Block / Input / Net 经 Driver 类门面（PR-D3 / B3）
 *
 * Common 只见 HalInput* / HalNet*；驱动私有头不进本文件。
 * 板包用 TOY_BOARD_HAS_* 勾选；Duo S 等命令行靶不探 virtio（避免误扫 MMIO）。
 */
#include "Hal.h"
#include "BoardConfig.h"
#include "Driver.h"
#include "DriverInput.h"
#include "DriverNet.h"
#if TOY_BOARD_HAS_BLOCK
#include "VirtioBlock.h"
#endif
#if TOY_BOARD_HAS_FRAMEBUFFER
#include "VirtioInput.h"
#endif
#if TOY_BOARD_HAS_NET
#include "VirtioNet.h"
#endif

void HalDriverRegister(void) {
#if TOY_BOARD_HAS_BLOCK
    VirtioBlockRegister();
#endif
#if TOY_BOARD_HAS_FRAMEBUFFER
    VirtioInputRegister();
#endif
#if TOY_BOARD_HAS_NET
    VirtioNetRegister();
#endif
}

int HalBlockInit(void) {
#if TOY_BOARD_HAS_BLOCK
    return VirtioBlockInit();
#else
    return 0;
#endif
}

int HalUsbInit(void) {
#if TOY_BOARD_HAS_FRAMEBUFFER
    return VirtioInputInit();
#else
    return 0;
#endif
}

int HalUsbMscInit(void) {
    return -1;
}

int HalUsbMscReady(void) {
    return 0;
}

int HalUsbMscScan(void) {
    return -1;
}

int HalUsbMscClaim(void) {
    return -1;
}

int HalUsbMscCapacity(void) {
    return -1;
}

UINT32 HalUsbMscBlockCount(void) {
    return 0;
}

UINT32 HalUsbMscBlockSize(void) {
    return 0;
}

void HalInputArmIrq(void) {
}

void HalInputInitMouseDeferred(void) {
}

void HalInputMouseHandoffDesktop(UINT32 CursorX, UINT32 CursorY) {
    (void)CursorX;
    (void)CursorY;
}

void HalInputPoll(void) {
    ToyDriverInputPoll();
}

void HalInputDiagFormat(char *Buf, int Max) {
    if (Buf && Max > 0) {
        Buf[0] = 0;
    }
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
#if TOY_BOARD_HAS_NET
    return VirtioNetInit();
#else
    return 0;
#endif
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
