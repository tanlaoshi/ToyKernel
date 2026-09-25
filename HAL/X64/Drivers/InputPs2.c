/*
 * InputPs2.c — i8042 经 Driver Input 类（PR-H2 + PR-H-ps2-aux）
 *
 * 键盘 Set2 + Aux 触控板相对包 → mux；实现在 Ps2/。
 */
#include "Driver.h"
#include "DriverInput.h"
#include "InputPs2.h"
#include "VirtualMemory.h"
#include "Hal.h"
#include "Ps2/Ps2Private.h"

static void Ps2Poll(void) {
    int Guard = 64;

    SpinLockAcquire(&gPs2Lock);
    while (Guard-- > 0) {
        UINT8 St = HalIoRead8(PS2_STATUS);
        UINT8 B;

        if (Ps2StatusLooksDead(St) || (St & STATUS_OBF) == 0) {
            break;
        }
        B = HalIoRead8(PS2_DATA);
        if (St & STATUS_MOUSE) {
            /* 未认 Aux 也记账，Shell `ps2` 的 bytes= 可证有没有 Aux 流量 */
            Ps2AuxFeed(B);
            continue;
        }
        Ps2KbdFeed(B);
    }
    SpinLockRelease(&gPs2Lock);
}

static int Ps2KeyboardDequeue(HAL_KEYBOARD_REPORT *Report) {
    int Ok;

    SpinLockAcquire(&gPs2Lock);
    Ok = Ps2KbdDequeue(Report);
    SpinLockRelease(&gPs2Lock);
    return Ok;
}

static int Ps2MousePresent(void) {
    return Ps2AuxPresent();
}

static int Ps2MouseDequeue(HAL_MOUSE_REPORT *Report) {
    int Ok;

    SpinLockAcquire(&gPs2Lock);
    Ok = Ps2AuxDequeue(Report);
    SpinLockRelease(&gPs2Lock);
    return Ok;
}

static const INPUT_BACKEND gPs2Backend = {
    .Poll = Ps2Poll,
    .KeyboardDequeue = Ps2KeyboardDequeue,
    .KeyboardSetLeds = 0,
    .MousePresent = Ps2MousePresent,
    .MouseDequeue = Ps2MouseDequeue,
};

static int Ps2DriverProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPrivate) {
    (void)Self;
    (void)BusCtx;
    if (!VirtualMemoryEnabled()) {
        return -1;
    }
    if (gPs2Ready) {
        if (OutPrivate) {
            *OutPrivate = 0;
        }
        return 0;
    }
    if (!Ps2InitHw()) {
        return -1;
    }
    gPs2Ready = 1;
    Ps2KbdResetState();
    if (OutPrivate) {
        *OutPrivate = 0;
    }
    return 0;
}

static int Ps2DriverBind(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
    return ToyDriverInputAttach(&gPs2Backend);
}

static void Ps2DriverRemove(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
    gPs2Ready = 0;
    gPs2AuxReady = 0;
}

static const TOY_DRIVER gPs2Driver = {
    .Name = "ps2-kbd",
    .Class = TOY_DRIVER_CLASS_INPUT,
    .Match = 0,
    .Probe = Ps2DriverProbe,
    .Bind = Ps2DriverBind,
    .Remove = Ps2DriverRemove,
};

void InputPs2Register(void) {
    (void)ToyDriverRegister(&gPs2Driver);
}

void Ps2MouseHandoffDesktop(UINT32 CursorX, UINT32 CursorY) {
    SpinLockAcquire(&gPs2Lock);
    Ps2AuxHandoffDesktop(CursorX, CursorY);
    SpinLockRelease(&gPs2Lock);
}
