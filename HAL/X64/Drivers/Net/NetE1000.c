/*
 * NetE1000.c — e1000 / e1000e 经 Driver Net 类注册（PR-H4 / PR-N-nic-e1000）
 *
 * Probe 只认 PCI e1000*；Bind → NetAttachNic(NIC_L2)。
 * 无卡 → Probe 失败，不挡桌面。lsdev 名随芯片：e1000 / e1000e。
 */
#include "Driver.h"
#include "DriverNic.h"
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

static int E1000NicSendFrame(const UINT8 *Frame, UINTN FrameLen) {
    return E1000SendFrame(Frame, FrameLen);
}

static void E1000NicPoll(void) {
    E1000Poll();
}

static void E1000NicGetMac(UINT8 Mac[6]) {
    E1000GetMac(Mac);
}

static int E1000NicGetLink(int *Up, UINT32 *Mbps, int *FullDuplex) {
    return E1000GetLink(Up, Mbps, FullDuplex);
}

static const NIC_L2 gE1000NicL2 = {
    .SendFrame = E1000NicSendFrame,
    .Poll = E1000NicPoll,
    .GetMac = E1000NicGetMac,
    .GetLink = E1000NicGetLink,
};

static int E1000DriverProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPrivate) {
    (void)Self;
    (void)BusCtx;

    if (E1000Ready()) {
        RefreshDriverName();
        if (OutPrivate) {
            *OutPrivate = 0;
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
    if (OutPrivate) {
        *OutPrivate = 0;
    }
    return 0;
}

static int E1000DriverBind(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
    if (!E1000Ready()) {
        return -1;
    }
    return NetAttachNic(&gE1000NicL2);
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
