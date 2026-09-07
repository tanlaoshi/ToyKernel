/*
 * NetE1000.c — e1000 经 Driver Net 类注册（PR-H4）
 *
 * Probe 只认 PCI e1000；Bind 挂上与 virtio-net 同一套 Net.c 协议栈。
 * 无卡 → Probe 失败，不挡桌面。
 */
#include "Driver.h"
#include "DriverNet.h"
#include "E1000.h"
#include "Net.h"
#include "VirtualMemory.h"
#include "Hal.h"

static int E1000DriverProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPriv) {
    (void)Self;
    (void)BusCtx;

    if (E1000Ready()) {
        if (OutPriv) {
            *OutPriv = 0;
        }
        return 0;
    }
    if (!VirtualMemoryEnabled()) {
        return -1;
    }
    if (!E1000Setup()) {
        return -1;
    }
    if (OutPriv) {
        *OutPriv = 0;
    }
    return 0;
}

static int E1000DriverBind(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
    return NetBindE1000();
}

static void E1000DriverRemove(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
}

static const TOY_DRIVER gE1000Driver = {
    .Name = "e1000",
    .Class = TOY_DRIVER_CLASS_NET,
    .Match = 0,
    .Probe = E1000DriverProbe,
    .Bind = E1000DriverBind,
    .Remove = E1000DriverRemove,
};

void E1000DriverRegister(void) {
    (void)ToyDriverRegister(&gE1000Driver);
}
