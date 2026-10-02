/*
 * NetIwl.c — iwl8265 Bind → NetAttachNic（PR-N-wifi-2；需 WPA2 关联）
 */
#include "Driver.h"
#include "DriverNic.h"
#include "Iwl.h"
#include "Net.h"
#include "VirtualMemory.h"

static int IwlNicSendFrame(const UINT8 *Frame, UINTN FrameLen) {
    return IwlSendFrame(Frame, FrameLen);
}

static void IwlNicPoll(void) {
    IwlPoll();
}

static void IwlNicGetMac(UINT8 Mac[6]) {
    IwlGetMac(Mac);
}

static int IwlNicGetLink(int *Up, UINT32 *Mbps, int *FullDuplex) {
    return IwlGetLink(Up, Mbps, FullDuplex);
}

static const NIC_L2 gIwlNicL2 = {
    .SendFrame = IwlNicSendFrame,
    .Poll = IwlNicPoll,
    .GetMac = IwlNicGetMac,
    .GetLink = IwlNicGetLink,
};

static int IwlDriverProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPrivate) {
    (void)Self;
    (void)BusCtx;

    if (!VirtualMemoryEnabled()) {
        return -1;
    }
    /* 每次 Probe 进 IwlSetup：允许 FW 晚到 / alive=0 再试 */
    if (!IwlSetup()) {
        return -1;
    }
    if (!IwlReady()) {
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
    if (!IwlAssociated()) {
        /* lsdev 仍可见；未关联则不挂 L2 */
        return 0;
    }
    /* WPA2 已关联：挂无线槽；有线仍在时出站优先有线 */
    return NetAttachNicKind(&gIwlNicL2, NET_NIC_KIND_WIFI);
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

/* Worker：后台上片；成功则 NetAttach（Probe 时尚未 assoc） */
void IwlNetBgPump(void) {
    static int gAttached;

    if (IwlBgBusy()) {
        (void)IwlBgStep();
    }
    if (!gAttached && IwlAssociated()) {
        gAttached = 1;
        (void)NetAttachNicKind(&gIwlNicL2, NET_NIC_KIND_WIFI);
    }
}

int IwlNetBgBusy(void) {
    return IwlBgBusy();
}
