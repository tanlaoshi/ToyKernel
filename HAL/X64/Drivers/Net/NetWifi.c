/*
 * NetWifi.c — rtl8188eu 经 Driver Net 类注册（PR-N-wifi-1）
 *
 * Probe 认 USB 棒；Bind 只上线 lsdev，**不** NetAttachNic（wifi-2）。
 */
#include "Driver.h"
#include "Wifi.h"

static int WifiDriverProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPrivate) {
    (void)Self;
    (void)BusCtx;

    if (WifiReady()) {
        if (OutPrivate) {
            *OutPrivate = 0;
        }
        return 0;
    }
    if (!WifiSetup()) {
        return -1;
    }
    if (OutPrivate) {
        *OutPrivate = 0;
    }
    return 0;
}

static int WifiDriverBind(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
    if (!WifiReady()) {
        return -1;
    }
    /* wifi-1：不上 Net；wifi-2 再 NetAttachNic */
    return 0;
}

static void WifiDriverRemove(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
}

static const TOY_DRIVER gWifiDriver = {
    .Name = "rtl8188eu",
    .Class = TOY_DRIVER_CLASS_NET,
    .Match = 0,
    .Probe = WifiDriverProbe,
    .Bind = WifiDriverBind,
    .Remove = WifiDriverRemove,
};

void WifiDriverRegister(void) {
    (void)ToyDriverRegister(&gWifiDriver);
}
