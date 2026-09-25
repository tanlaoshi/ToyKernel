/*
 * NetAlx.c — alx 经 Driver Net 类注册（PR-N-alx-1）
 *
 * Probe 认 PCI 1969:1091…；Bind 仅占 lsdev 槽，不 NetAttachNic（→ alx-2）。
 */
#include "Driver.h"
#include "Alx.h"
#include "VirtualMemory.h"

static int AlxDriverProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPrivate) {
    (void)Self;
    (void)BusCtx;

    if (AlxReady()) {
        if (OutPrivate) {
            *OutPrivate = 0;
        }
        return 0;
    }
    if (!VirtualMemoryEnabled()) {
        return -1;
    }
    if (!AlxSetup()) {
        return -1;
    }
    if (OutPrivate) {
        *OutPrivate = 0;
    }
    return 0;
}

static int AlxDriverBind(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
    if (!AlxReady()) {
        return -1;
    }
    /* PR-N-alx-1：可见于 lsdev；协议栈挂接留给 alx-2 */
    return 0;
}

static void AlxDriverRemove(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
}

static const TOY_DRIVER gAlxDriver = {
    .Name = "alx",
    .Class = TOY_DRIVER_CLASS_NET,
    .Match = 0,
    .Probe = AlxDriverProbe,
    .Bind = AlxDriverBind,
    .Remove = AlxDriverRemove,
};

void AlxDriverRegister(void) {
    (void)ToyDriverRegister(&gAlxDriver);
}
