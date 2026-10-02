/*
 * HalDevicesInput.c — x86：Input / EHCI·UHCI·PS2 诊断门面（PR-S3-haldev-1）
 */
#include "Hal.h"
#include "DriverInput.h"
#include "InputXhci.h"
#include "InputPs2.h"
#include "Ehci.h"
#include "Uhci.h"
#include "XHCI.h"

void XhciDiagFormat(char *Buf, int Max);
void XhciMouseHandoffDesktop(UINT32 CursorX, UINT32 CursorY);

void HalInputArmIrq(void) {
    InputXhciArmIrq();
}

void HalInputInitializeMouseDeferred(void) {
    /*
     * 刀 #117：勿在 MSC 认盘后再扫 hub 鼠（会 Reset 子口 → 桌面假死）。
     * 枚举期已绑则此处为空操作；仅漏绑时补一次（少见）。
     */
    if (!XhciMousePresent()) {
        XhciInitMouseDeferred();
    }
}

void HalInputMouseHandoffDesktop(UINT32 CursorX, UINT32 CursorY) {
    XhciMouseHandoffDesktop(CursorX, CursorY);
    Ps2MouseHandoffDesktop(CursorX, CursorY);
}

void HalInputPoll(void) {
    ToyDriverInputPoll();
}

void HalInputDiagFormat(char *Buf, int Max) {
    XhciDiagFormat(Buf, Max);
}

void HalEhciDiagFormat(char *Buf, int Max) {
    EhciDiagFormat(Buf, Max);
}

int HalEhciHidRetry(void) {
    return EhciHidBringup() ? 0 : -1;
}

/* 1=Bulk 已发出；0=未认；-1=Bulk 失败（见 ehci err=） */
int HalEhciFtdiPing(void) {
    if (!EhciFtdiReady()) {
        return 0;
    }
    if (EhciFtdiWrite("\r\n*** FTDI 115200 ***\r\n") < 0) {
        return -1;
    }
    return 1;
}

void HalUhciDiagFormat(char *Buf, int Max) {
    UhciDiagFormat(Buf, Max);
}

void HalPs2DiagFormat(char *Buf, int Max) {
    Ps2DiagFormat(Buf, Max);
}

int HalPs2AuxRetry(void) {
    return Ps2AuxRetry();
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
