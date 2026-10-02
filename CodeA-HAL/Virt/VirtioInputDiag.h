/*
 * VirtioInputDiag.h — PR-V-input-diag：virtio-input 只读计数
 */
#ifndef HAL_VIRTIO_INPUT_DIAG_H
#define HAL_VIRTIO_INPUT_DIAG_H

#include "BootTypes.h"

void VirtioInputDiagSetPresent(int KeyboardOn, int TabletOn);
void VirtioInputDiagNotePoll(UINT16 KeyboardEvents, UINT16 TabletEvents);
void VirtioInputDiagNoteKeyboardPush(void);
void VirtioInputDiagNoteMousePush(void);
void VirtioInputDiagNoteKeyboardDequeue(void);
void VirtioInputDiagNoteMouseDequeue(void);
void VirtioInputDiagFormat(char *Buf, int Max);
/* BSP 定时器钩：不依赖 Poll，用于确认 p=0 / AP tick */
void VirtioInputDiagOnTimer(void);

#endif
