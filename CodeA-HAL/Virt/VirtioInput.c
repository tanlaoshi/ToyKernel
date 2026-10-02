/*
 * VirtioInput.c — virtio-input MMIO 驱动注册与轮询（PR-S3-virtioinput-1）
 *
 * 事件解码见 VirtioInputEv.c。
 */
#include "VirtioInput.h"
#include "VirtioInputDiag.h"
#include "VirtioMmio.h"
#include "HalSerial.h"
#include "HalVideo.h"
#include "PhysicalMemory.h"
#include "BootInfo.h"
#include "Driver.h"
#include "DriverInput.h"
#include "HalDevices.h"
#include "ToySerialLog.h"
#include "VirtioInputPrivate.h"

VIRTIO_MMIO_DEV gKbd;
VIRTIO_MMIO_DEV gTab;
int gKbdOn;
int gTabOn;

HAL_KEYBOARD_REPORT gKbdQ[KBD_Q_SIZE];
UINT32 gKbdHead;
UINT32 gKbdTail;
HAL_KEYBOARD_REPORT gKbdCur;

HAL_MOUSE_REPORT gMouseQ[MOUSE_Q_SIZE];
UINT32 gMouseHead;
UINT32 gMouseTail;
INT32 gAbsMinX;
INT32 gAbsMaxX;
INT32 gAbsMinY;
INT32 gAbsMaxY;
INT32 gAbsX;
INT32 gAbsY;
UINT8 gButtons;
INT8 gWheelAcc; /* PR-I1：EV_REL REL_WHEEL 累加，SYN/按钮时随报告送出 */
int gHaveAbs;

VIRTIO_INPUT_EVENT *gKbdEvBuf;
VIRTIO_INPUT_EVENT *gTabEvBuf;

/* Linux KEY_* → HID Usage（子集） */

static void ReadAbsInfo(UINT64 Base, UINT8 Axis, INT32 *Min, INT32 *Max) {
    volatile VIRTIO_INPUT_CFG *Cfg =
        (volatile VIRTIO_INPUT_CFG *)(UINTN)(Base + VIRTIO_INPUT_CFG_SIZE);
    INT32 *Vals;

    Cfg->Select = VIRTIO_INPUT_CFG_ABS;
    Cfg->Subsel = Axis;
    __asm__ volatile("" ::: "memory");
    if (Cfg->Size < 16) {
        *Min = 0;
        *Max = 32767;
        return;
    }
    Vals = (INT32 *)(void *)Cfg->Data;
    *Min = Vals[0];
    *Max = Vals[1];
}

typedef struct {
    UINT64 KbdBase;
    UINT64 TabBase;
} IN_SCAN;

static void InScanCb(UINT64 Base, UINT32 DeviceId, void *Ctx) {
    IN_SCAN *S = (IN_SCAN *)Ctx;
    volatile VIRTIO_INPUT_CFG *Cfg;
    char Name[64];
    UINT32 i;

    if (DeviceId != VIRTIO_DEV_INPUT) {
        return;
    }
    Cfg = (volatile VIRTIO_INPUT_CFG *)(UINTN)(Base + VIRTIO_INPUT_CFG_SIZE);
    Cfg->Select = VIRTIO_INPUT_CFG_ID_NAME;
    Cfg->Subsel = 0;
    __asm__ volatile("" ::: "memory");
    for (i = 0; i < 63 && i < Cfg->Size; i++) {
        Name[i] = (char)Cfg->Data[i];
    }
    Name[i] = 0;
    /* QEMU：名含 Keyboard / Tablet */
    if (S->KbdBase == 0) {
        for (i = 0; Name[i]; i++) {
            if ((Name[i] == 'K' || Name[i] == 'k') && Name[i + 1] == 'e' &&
                Name[i + 2] == 'y') {
                S->KbdBase = Base;
                return;
            }
        }
    }
    if (S->TabBase == 0) {
        for (i = 0; Name[i]; i++) {
            if ((Name[i] == 'T' || Name[i] == 't') && Name[i + 1] == 'a' &&
                Name[i + 2] == 'b') {
                S->TabBase = Base;
                return;
            }
        }
    }
    /* 无名时按发现顺序：先键盘后 tablet */
    if (S->KbdBase == 0) {
        S->KbdBase = Base;
    } else if (S->TabBase == 0 && Base != S->KbdBase) {
        S->TabBase = Base;
    }
}

static void VirtioInputPoll(void) {
    UINT16 KeyboardEvents = 0;
    UINT16 TabletEvents = 0;

    if (gKbdOn) {
        KeyboardEvents = DrainDev(&gKbd, gKbdEvBuf, 0);
    }
    if (gTabOn) {
        TabletEvents = DrainDev(&gTab, gTabEvBuf, 1);
    }
    VirtioInputDiagNotePoll(KeyboardEvents, TabletEvents);
}

static int VirtioInputKeyboardDequeue(HAL_KEYBOARD_REPORT *Report) {
    if (!Report || gKbdTail == gKbdHead) {
        return 0;
    }
    *Report = gKbdQ[gKbdTail];
    gKbdTail = (gKbdTail + 1) % KBD_Q_SIZE;
    VirtioInputDiagNoteKeyboardDequeue();
    return 1;
}

static int VirtioInputMousePresent(void) {
    return gTabOn;
}

static int VirtioInputMouseDequeue(HAL_MOUSE_REPORT *Report) {
    if (!Report || gMouseTail == gMouseHead) {
        return 0;
    }
    *Report = gMouseQ[gMouseTail];
    gMouseTail = (gMouseTail + 1) % MOUSE_Q_SIZE;
    VirtioInputDiagNoteMouseDequeue();
    return 1;
}

static const INPUT_BACKEND gInputBackend = {
    .Poll = VirtioInputPoll,
    .KeyboardDequeue = VirtioInputKeyboardDequeue,
    .KeyboardSetLeds = 0,
    .MousePresent = VirtioInputMousePresent,
    .MouseDequeue = VirtioInputMouseDequeue,
};

static int VirtioInputDriverProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPrivate) {
    IN_SCAN S;
    UINT8 *Page;

    (void)Self;
    (void)BusCtx;
    if (gKbdOn || gTabOn) {
        if (OutPrivate) {
            *OutPrivate = 0;
        }
        return 0;
    }

    S.KbdBase = 0;
    S.TabBase = 0;
    VirtioMmioScan(InScanCb, &S);

    Page = (UINT8 *)PhysicalMemoryAllocatePages(2);
    if (!Page) {
        return -1;
    }
    gKbdEvBuf = (VIRTIO_INPUT_EVENT *)(UINTN)Page;
    gTabEvBuf = (VIRTIO_INPUT_EVENT *)(UINTN)(Page + PAGE_SIZE);

    if (S.KbdBase) {
        if (VirtioMmioSetupQueue(&gKbd, S.KbdBase, VIRTIO_DEV_INPUT, 8, 0) == 0) {
            RefillQueue(&gKbd, gKbdEvBuf, gKbd.QueueSize);
            gKbdOn = 1;
            ToyLogDrv("Boot: VirtIO-Input Keyboard\n");
        }
    }
    if (S.TabBase) {
        if (VirtioMmioSetupQueue(&gTab, S.TabBase, VIRTIO_DEV_INPUT, 8, 0) == 0) {
            ReadAbsInfo(S.TabBase, ABS_X, &gAbsMinX, &gAbsMaxX);
            ReadAbsInfo(S.TabBase, ABS_Y, &gAbsMinY, &gAbsMaxY);
            RefillQueue(&gTab, gTabEvBuf, gTab.QueueSize);
            gTabOn = 1;
            ToyLogDrv("Boot: VirtIO-Input Tablet\n");
        }
    }
    if (!(gKbdOn || gTabOn)) {
        return -1;
    }
    VirtioInputDiagSetPresent(gKbdOn, gTabOn);
    if (OutPrivate) {
        *OutPrivate = 0;
    }
    return 0;
}

static int VirtioInputDriverBind(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
    return ToyDriverInputAttach(&gInputBackend);
}

static void VirtioInputDriverRemove(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
    gKbdOn = 0;
    gTabOn = 0;
}

static const TOY_DRIVER gVirtioInputDriver = {
    .Name = "virtio-input",
    .Class = TOY_DRIVER_CLASS_INPUT,
    .Match = 0,
    .Probe = VirtioInputDriverProbe,
    .Bind = VirtioInputDriverBind,
    .Remove = VirtioInputDriverRemove,
};

void VirtioInputRegister(void) {
    (void)ToyDriverRegister(&gVirtioInputDriver);
}

int VirtioInputInit(void) {
    (void)ToyDriverProbeClass(TOY_DRIVER_CLASS_INPUT);
    return ToyDriverInputReady() ? 0 : -1;
}
