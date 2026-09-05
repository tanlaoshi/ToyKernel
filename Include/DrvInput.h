/*
 * DrvInput.h — Input 类适配（PR-D3）
 *
 * 驱动 Bind 时调用 ToyDrvInputAttach；Common 仍只见 HalInput*。
 */
#ifndef DRV_INPUT_H
#define DRV_INPUT_H

#include "HalDevices.h"

typedef struct {
    void (*Poll)(void);
    int (*KeyboardDequeue)(HAL_KEYBOARD_REPORT *Report);
    /* 可选；NULL → HalKeyboardSetLeds 返回 -1 */
    int (*KeyboardSetLeds)(UINT8 Leds);
    int (*MousePresent)(void);
    int (*MouseDequeue)(HAL_MOUSE_REPORT *Report);
} INPUT_BACKEND;

int ToyDrvInputAttach(const INPUT_BACKEND *Backend);
int ToyDrvInputReady(void);

void ToyDrvInputPoll(void);
int ToyDrvInputKeyboardDequeue(HAL_KEYBOARD_REPORT *Report);
int ToyDrvInputKeyboardSetLeds(UINT8 Leds);
int ToyDrvInputMousePresent(void);
int ToyDrvInputMouseDequeue(HAL_MOUSE_REPORT *Report);

#endif
