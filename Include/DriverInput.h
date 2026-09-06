/*
 * DriverInput.h — Input 类适配（PR-D3）
 *
 * 驱动 Bind 时调用 ToyDriverInputAttach；Common 仍只见 HalInput*。
 */
#ifndef DRIVER_INPUT_H
#define DRIVER_INPUT_H

#include "HalDevices.h"

typedef struct {
    void (*Poll)(void);
    int (*KeyboardDequeue)(HAL_KEYBOARD_REPORT *Report);
    /* 可选；NULL → HalKeyboardSetLeds 返回 -1 */
    int (*KeyboardSetLeds)(UINT8 Leds);
    int (*MousePresent)(void);
    int (*MouseDequeue)(HAL_MOUSE_REPORT *Report);
} INPUT_BACKEND;

int ToyDriverInputAttach(const INPUT_BACKEND *Backend);
int ToyDriverInputReady(void);

void ToyDriverInputPoll(void);
int ToyDriverInputKeyboardDequeue(HAL_KEYBOARD_REPORT *Report);
int ToyDriverInputKeyboardSetLeds(UINT8 Leds);
int ToyDriverInputMousePresent(void);
int ToyDriverInputMouseDequeue(HAL_MOUSE_REPORT *Report);

#endif
