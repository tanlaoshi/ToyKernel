/*
 * DriverInput.c — Input 类适配层（PR-D3；PR-H-input-mux：多 backend 聚合）
 */
#include "DriverInput.h"
#include "Debug.h"

#define INPUT_BACKEND_MAX 4

static const INPUT_BACKEND *gInputBackends[INPUT_BACKEND_MAX];
static UINTN gInputBackendCount;

int ToyDriverInputAttach(const INPUT_BACKEND *Backend) {
    UINTN i;

    if (!Backend || !Backend->Poll || !Backend->KeyboardDequeue ||
        !Backend->MousePresent || !Backend->MouseDequeue) {
        DebugWrite("drv-input: bad backend\n");
        return -1;
    }
    for (i = 0; i < gInputBackendCount; i++) {
        if (gInputBackends[i] == Backend) {
            return 0; /* 已挂 */
        }
    }
    if (gInputBackendCount >= INPUT_BACKEND_MAX) {
        DebugWrite("drv-input: backend full\n");
        return -1;
    }
    gInputBackends[gInputBackendCount++] = Backend;
    return 0;
}

int ToyDriverInputReady(void) {
    return gInputBackendCount != 0;
}

void ToyDriverInputPoll(void) {
    UINTN i;

    for (i = 0; i < gInputBackendCount; i++) {
        if (gInputBackends[i]->Poll) {
            gInputBackends[i]->Poll();
        }
    }
}

int ToyDriverInputKeyboardDequeue(HAL_KEYBOARD_REPORT *Report) {
    UINTN i;

    if (!Report) {
        return 0;
    }
    for (i = 0; i < gInputBackendCount; i++) {
        if (gInputBackends[i]->KeyboardDequeue &&
            gInputBackends[i]->KeyboardDequeue(Report)) {
            return 1;
        }
    }
    return 0;
}

int ToyDriverInputKeyboardSetLeds(UINT8 Leds) {
    UINTN i;
    int Ok = -1;

    for (i = 0; i < gInputBackendCount; i++) {
        if (gInputBackends[i]->KeyboardSetLeds &&
            gInputBackends[i]->KeyboardSetLeds(Leds) == 0) {
            Ok = 0;
        }
    }
    return Ok;
}

int ToyDriverInputMousePresent(void) {
    UINTN i;

    for (i = 0; i < gInputBackendCount; i++) {
        if (gInputBackends[i]->MousePresent &&
            gInputBackends[i]->MousePresent()) {
            return 1;
        }
    }
    return 0;
}

int ToyDriverInputMouseDequeue(HAL_MOUSE_REPORT *Report) {
    UINTN i;

    if (!Report) {
        return 0;
    }
    for (i = 0; i < gInputBackendCount; i++) {
        if (gInputBackends[i]->MouseDequeue &&
            gInputBackends[i]->MouseDequeue(Report)) {
            return 1;
        }
    }
    return 0;
}
