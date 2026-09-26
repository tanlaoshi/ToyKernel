/*
 * NetRtl.c — r8169 经 Driver Net 类注册（PR-N-rtl-1）
 *
 * Probe 认 PCI 10EC:8168…；Bind 仅占 lsdev 槽，不 NetAttachNic（→ rtl-2）。
 */
#include "Driver.h"
#include "Rtl.h"
#include "VirtualMemory.h"

static int RtlDriverProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPrivate) {
    (void)Self;
    (void)BusCtx;

    if (RtlReady()) {
        if (OutPrivate) {
            *OutPrivate = 0;
        }
        return 0;
    }
    if (!VirtualMemoryEnabled()) {
        return -1;
    }
    if (!RtlSetup()) {
        return -1;
    }
    if (OutPrivate) {
        *OutPrivate = 0;
    }
    return 0;
}

static int RtlDriverBind(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
    if (!RtlReady()) {
        return -1;
    }
    /* PR-N-rtl-1：可见于 lsdev；协议栈挂接留给 rtl-2 */
    return 0;
}

static void RtlDriverRemove(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
}

static const TOY_DRIVER gRtlDriver = {
    .Name = "r8169",
    .Class = TOY_DRIVER_CLASS_NET,
    .Match = 0,
    .Probe = RtlDriverProbe,
    .Bind = RtlDriverBind,
    .Remove = RtlDriverRemove,
};

void RtlDriverRegister(void) {
    (void)ToyDriverRegister(&gRtlDriver);
}
