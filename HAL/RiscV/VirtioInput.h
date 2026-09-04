/*
 * VirtioInput.h — virtio-keyboard / tablet（PR-V3）
 */
#ifndef HAL_VIRTIO_INPUT_H
#define HAL_VIRTIO_INPUT_H

#include "HalDevices.h"

int VirtioInputInit(void);
void VirtioInputPoll(void);
int VirtioInputKeyboardDequeue(HAL_KEYBOARD_REPORT *Report);
int VirtioInputMousePresent(void);
int VirtioInputMouseDequeue(HAL_MOUSE_REPORT *Report);

#endif
