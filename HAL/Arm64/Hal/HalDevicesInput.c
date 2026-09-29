/*
 * HalDevicesInput.c — Arm64：Input / EHCI·UHCI·PS2 桩（PR-S3-haldev-1）
 */
#include "Hal.h"
#include "DriverInput.h"
#include "VirtioInputDiag.h"

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
    VirtioInputDiagFormat(Buf, Max);
}

void HalEhciDiagFormat(char *Buf, int Max) {
    if (Buf && Max > 0) {
        Buf[0] = 0;
        if (Max > 8) {
            Buf[0] = 'r';
            Buf[1] = 'e';
            Buf[2] = 'a';
            Buf[3] = 'd';
            Buf[4] = 'y';
            Buf[5] = '=';
            Buf[6] = '0';
            Buf[7] = 0;
        }
    }
}

int HalEhciHidRetry(void) {
    return -1;
}

int HalEhciFtdiPing(void) {
    return 0;
}

void HalUhciDiagFormat(char *Buf, int Max) {
    if (Buf && Max > 0) {
        Buf[0] = 0;
        if (Max > 8) {
            Buf[0] = 'r';
            Buf[1] = 'e';
            Buf[2] = 'a';
            Buf[3] = 'd';
            Buf[4] = 'y';
            Buf[5] = '=';
            Buf[6] = '0';
            Buf[7] = 0;
        }
    }
}

void HalPs2DiagFormat(char *Buf, int Max) {
    if (Buf && Max > 0) {
        Buf[0] = 0;
        if (Max > 16) {
            Buf[0] = 'k';
            Buf[1] = 'b';
            Buf[2] = 'd';
            Buf[3] = '=';
            Buf[4] = '0';
            Buf[5] = ' ';
            Buf[6] = 'a';
            Buf[7] = 'u';
            Buf[8] = 'x';
            Buf[9] = '=';
            Buf[10] = '0';
            Buf[11] = 0;
        }
    }
}

int HalPs2AuxRetry(void) {
    return 0;
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
