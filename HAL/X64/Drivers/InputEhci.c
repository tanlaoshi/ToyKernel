/*
 * InputEhci.c — ehci 经 Driver Input 类注册（PR-H-ehci-1/2）
 *
 * ehci-2：Bind → ToyDriverInputAttach（mux 与 ps2/xhci 并存）。
 */
#include "Driver.h"
#include "DriverInput.h"
#include "Ehci.h"
#include "VirtualMemory.h"

static void EhciInputPoll(void) {
    if (EhciReady()) {
        EhciHidPoll();
    }
}

static int EhciKeyboardDequeue(HAL_KEYBOARD_REPORT *Report) {
    UINT8 Raw[8];
    int i;

    if (!Report || !EhciHidReady()) {
        return 0;
    }
    if (!EhciHidKeyboardDequeue(Raw)) {
        return 0;
    }
    Report->ModifierKeys = Raw[0];
    Report->Reserved = Raw[1];
    for (i = 0; i < 6; i++) {
        Report->KeyCode[i] = Raw[2 + i];
    }
    return 1;
}

static int EhciMousePresentWrap(void) {
    return EhciHidMousePresent();
}

static int EhciMouseDequeue(HAL_MOUSE_REPORT *Report) {
    UINT32 X;
    UINT32 Y;
    UINT8 Btn;
    INT8 Wheel;

    if (!Report || !EhciHidMousePresent()) {
        return 0;
    }
    if (!EhciHidMouseDequeue(&X, &Y, &Btn, &Wheel)) {
        return 0;
    }
    Report->Buttons = Btn;
    Report->X = X;
    Report->Y = Y;
    Report->Wheel = Wheel;
    Report->Absolute = 0;
    return 1;
}

static const INPUT_BACKEND gEhciInputBackend = {
    .Poll = EhciInputPoll,
    .KeyboardDequeue = EhciKeyboardDequeue,
    .KeyboardSetLeds = 0,
    .MousePresent = EhciMousePresentWrap,
    .MouseDequeue = EhciMouseDequeue,
};

static int EhciDriverProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPrivate) {
    (void)Self;
    (void)BusCtx;

    if (EhciReady()) {
        if (OutPrivate) {
            *OutPrivate = 0;
        }
        return 0;
    }
    if (!VirtualMemoryEnabled()) {
        return -1;
    }
    if (!EhciSetup()) {
        return -1;
    }
    (void)EhciHidBringup(); /* 无 HID 仍占 lsdev；mux 空转 */
    if (OutPrivate) {
        *OutPrivate = 0;
    }
    return 0;
}

static int EhciDriverBind(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
    if (!EhciReady()) {
        return -1;
    }
    return ToyDriverInputAttach(&gEhciInputBackend);
}

static void EhciDriverRemove(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
}

static const TOY_DRIVER gEhciDriver = {
    .Name = "ehci",
    .Class = TOY_DRIVER_CLASS_INPUT,
    .Match = 0,
    .Probe = EhciDriverProbe,
    .Bind = EhciDriverBind,
    .Remove = EhciDriverRemove,
};

void InputEhciRegister(void) {
    (void)ToyDriverRegister(&gEhciDriver);
}
