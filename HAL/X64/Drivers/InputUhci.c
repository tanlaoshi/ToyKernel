/*
 * InputUhci.c — uhci 经 Driver 注册（PR-H-uhci-1：仅 Probe/CCS）
 */
#include "Driver.h"
#include "Uhci.h"

static int UhciDriverProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPrivate) {
    (void)Self;
    (void)BusCtx;

    if (UhciReady()) {
        if (OutPrivate) {
            *OutPrivate = 0;
        }
        return 0;
    }
    if (!UhciSetup()) {
        return -1;
    }
    if (OutPrivate) {
        *OutPrivate = 0;
    }
    return 0;
}

static int UhciDriverBind(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
    return UhciReady() ? 0 : -1;
}

static void UhciDriverRemove(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
}

static const TOY_DRIVER gUhciDriver = {
    .Name = "uhci",
    .Class = TOY_DRIVER_CLASS_INPUT,
    .Match = 0,
    .Probe = UhciDriverProbe,
    .Bind = UhciDriverBind,
    .Remove = UhciDriverRemove,
};

void InputUhciRegister(void) {
    (void)ToyDriverRegister(&gUhciDriver);
}
