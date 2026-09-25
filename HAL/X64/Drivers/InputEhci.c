/*
 * InputEhci.c — ehci 经 Driver Input 类注册（PR-H-ehci-1）
 *
 * Bind 占 lsdev 槽，不 ToyDriverInputAttach（→ ehci-2 HID）。
 */
#include "Driver.h"
#include "Ehci.h"
#include "VirtualMemory.h"

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
    return 0;
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
