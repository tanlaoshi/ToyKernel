/*
 * NetE1000.c — e1000 / e1000e 经 Driver Net 类注册（PR-H4 / PR-H4e-2）
 *
 * Probe 只认 PCI e1000*；Bind 挂上与 virtio-net 同一套 Net.c 协议栈。
 * 无卡 → Probe 失败，不挡桌面。lsdev 名随芯片：e1000 / e1000e。
 */
#include "Driver.h"
#include "DriverNet.h"
#include "E1000.h"
#include "Net.h"
#include "VirtualMemory.h"
#include "Hal.h"

static char gE1000DriverName[8] = "e1000";

static void RefreshDriverName(void) {
    const char *Chip = E1000ChipName();
    int i;

    for (i = 0; i < 7 && Chip[i]; i++) {
        gE1000DriverName[i] = Chip[i];
    }
    gE1000DriverName[i] = 0;
}

static int E1000DriverProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPriv) {
    (void)Self;
    (void)BusCtx;

    if (E1000Ready()) {
        RefreshDriverName();
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
    RefreshDriverName();
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
    .Name = gE1000DriverName,
    .Class = TOY_DRIVER_CLASS_NET,
    .Match = 0,
    .Probe = E1000DriverProbe,
    .Bind = E1000DriverBind,
    .Remove = E1000DriverRemove,
};

void E1000DriverRegister(void) {
    (void)ToyDriverRegister(&gE1000Driver);
}
