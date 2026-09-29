/*
 * VirtioInputPrivate.h — VirtioInput / VirtioInputEv 内部交接（PR-S3-virtioinput-1）
 */
#ifndef VIRTIO_INPUT_PRIVATE_H
#define VIRTIO_INPUT_PRIVATE_H

#include "VirtioMmio.h"
#include "DriverInput.h"
#include "HalDevices.h"

#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_REL 0x02
#define EV_ABS 0x03

#define ABS_X 0x00
#define ABS_Y 0x01
#define REL_WHEEL 0x08

#define VIRTIO_INPUT_CFG_ID_NAME 0x01
#define VIRTIO_INPUT_CFG_ABS     0x03
#define VIRTIO_INPUT_CFG_SIZE    0x100u

typedef struct {
    UINT16 Type;
    UINT16 Code;
    INT32  Value;
} __attribute__((packed)) VIRTIO_INPUT_EVENT;

typedef struct {
    UINT8 Select;
    UINT8 Subsel;
    UINT8 Size;
    UINT8 Reserved[5];
    UINT8 Data[128];
} __attribute__((packed)) VIRTIO_INPUT_CFG;

#define KBD_Q_SIZE 16
#define MOUSE_Q_SIZE 32


extern VIRTIO_MMIO_DEV gKbd;
extern VIRTIO_MMIO_DEV gTab;
extern int gKbdOn;
extern int gTabOn;
extern HAL_KEYBOARD_REPORT gKbdQ[KBD_Q_SIZE];
extern UINT32 gKbdHead;
extern UINT32 gKbdTail;
extern HAL_KEYBOARD_REPORT gKbdCur;
extern HAL_MOUSE_REPORT gMouseQ[MOUSE_Q_SIZE];
extern UINT32 gMouseHead;
extern UINT32 gMouseTail;
extern INT32 gAbsMinX;
extern INT32 gAbsMaxX;
extern INT32 gAbsMinY;
extern INT32 gAbsMaxY;
extern INT32 gAbsX;
extern INT32 gAbsY;
extern UINT8 gButtons;
extern INT8 gWheelAcc;
extern int gHaveAbs;
extern VIRTIO_INPUT_EVENT *gKbdEvBuf;
extern VIRTIO_INPUT_EVENT *gTabEvBuf;

UINT8 LinuxKeyToHid(UINT16 Code);
UINT8 LinuxModBit(UINT16 Code);
void KbdPush(void);
void MousePush(void);
void ApplyKey(UINT16 Code, INT32 Value);
void RefillQueue(VIRTIO_MMIO_DEV *Dev, VIRTIO_INPUT_EVENT *Buf, UINT16 Count);
UINT16 DrainDev(VIRTIO_MMIO_DEV *Dev, VIRTIO_INPUT_EVENT *Buf, int IsTab);

#endif
