/*
 * DriverInput.c — Input 类适配层（PR-D3）
 */
#include "DriverInput.h"
#include "Debug.h"

static const INPUT_BACKEND *gInputBackend;

int ToyDriverInputAttach(const INPUT_BACKEND *Backend) {
    if (!Backend || !Backend->Poll || !Backend->KeyboardDequeue ||
        !Backend->MousePresent || !Backend->MouseDequeue) {
        DebugWrite("drv-input: bad backend\n");
        return -1;
    }
    gInputBackend = Backend;
    return 0;
}

int ToyDriverInputReady(void) {
    return gInputBackend != 0;
}

void ToyDriverInputPoll(void) {
    if (gInputBackend && gInputBackend->Poll) {
        gInputBackend->Poll();
    }
}

int ToyDriverInputKeyboardDequeue(HAL_KEYBOARD_REPORT *Report) {
    if (!gInputBackend || !gInputBackend->KeyboardDequeue) {
        return 0;
    }
    return gInputBackend->KeyboardDequeue(Report);
}

int ToyDriverInputKeyboardSetLeds(UINT8 Leds) {
    if (!gInputBackend || !gInputBackend->KeyboardSetLeds) {
        return -1;
    }
    return gInputBackend->KeyboardSetLeds(Leds);
}

int ToyDriverInputMousePresent(void) {
    if (!gInputBackend || !gInputBackend->MousePresent) {
        return 0;
    }
    return gInputBackend->MousePresent();
}

int ToyDriverInputMouseDequeue(HAL_MOUSE_REPORT *Report) {
    if (!gInputBackend || !gInputBackend->MouseDequeue) {
        return 0;
    }
    return gInputBackend->MouseDequeue(Report);
}
