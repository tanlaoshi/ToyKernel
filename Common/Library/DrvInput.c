/*
 * DrvInput.c — Input 类适配层（PR-D3）
 */
#include "DrvInput.h"
#include "Debug.h"

static const INPUT_BACKEND *gInputBackend;

int ToyDrvInputAttach(const INPUT_BACKEND *Backend) {
    if (!Backend || !Backend->Poll || !Backend->KeyboardDequeue ||
        !Backend->MousePresent || !Backend->MouseDequeue) {
        DebugWrite("drv-input: bad backend\n");
        return -1;
    }
    gInputBackend = Backend;
    return 0;
}

int ToyDrvInputReady(void) {
    return gInputBackend != 0;
}

void ToyDrvInputPoll(void) {
    if (gInputBackend && gInputBackend->Poll) {
        gInputBackend->Poll();
    }
}

int ToyDrvInputKeyboardDequeue(HAL_KEYBOARD_REPORT *Report) {
    if (!gInputBackend || !gInputBackend->KeyboardDequeue) {
        return 0;
    }
    return gInputBackend->KeyboardDequeue(Report);
}

int ToyDrvInputKeyboardSetLeds(UINT8 Leds) {
    if (!gInputBackend || !gInputBackend->KeyboardSetLeds) {
        return -1;
    }
    return gInputBackend->KeyboardSetLeds(Leds);
}

int ToyDrvInputMousePresent(void) {
    if (!gInputBackend || !gInputBackend->MousePresent) {
        return 0;
    }
    return gInputBackend->MousePresent();
}

int ToyDrvInputMouseDequeue(HAL_MOUSE_REPORT *Report) {
    if (!gInputBackend || !gInputBackend->MouseDequeue) {
        return 0;
    }
    return gInputBackend->MouseDequeue(Report);
}
