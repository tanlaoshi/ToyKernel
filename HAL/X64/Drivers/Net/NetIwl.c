/*
 * NetIwl.c — iwl8265 经 Driver Net 类注册（PR-N-wifi-1）
 *
 * Probe 认 PCI 8086:24fd；Bind 只上 lsdev，**不** NetAttachNic（wifi-2）。
 */
#include "Driver.h"
#include "Iwl.h"

static int IwlDriverProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPrivate) {
    (void)Self;
    (void)BusCtx;

    if (IwlReady()) {
        if (OutPrivate) {
            *OutPrivate = 0;
        }
        return 0;
    }
    /* FS 前 NetInit 会探一次：无卡/未 Setup → 失败；HalIwlClaim 后再探 */
    if (!IwlSetup()) {
        return -1;
    }
    if (OutPrivate) {
        *OutPrivate = 0;
    }
    return 0;
}

static int IwlDriverBind(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
    if (!IwlReady()) {
        return -1;
    }
    return 0;
}

static void IwlDriverRemove(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
}

static const TOY_DRIVER gIwlDriver = {
    .Name = "iwl8265",
    .Class = TOY_DRIVER_CLASS_NET,
    .Match = 0,
    .Probe = IwlDriverProbe,
    .Bind = IwlDriverBind,
    .Remove = IwlDriverRemove,
};

void IwlDriverRegister(void) {
    (void)ToyDriverRegister(&gIwlDriver);
}
