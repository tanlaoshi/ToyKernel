/*
 * NetRtl.c — r8169 经 Driver Net 类注册（PR-N-rtl-2）
 *
 * Probe 认 PCI 10EC:8168…；Bind → NetAttachNic(NIC_L2)。
 */
#include "Driver.h"
#include "DriverNic.h"
#include "Rtl.h"
#include "Net.h"
#include "VirtualMemory.h"

static int RtlNicSendFrame(const UINT8 *Frame, UINTN FrameLen) {
    return RtlSendFrame(Frame, FrameLen);
}

static void RtlNicPoll(void) {
    RtlPoll();
}

static void RtlNicGetMac(UINT8 Mac[6]) {
    RtlGetMac(Mac);
}

static int RtlNicGetLink(int *Up, UINT32 *Mbps, int *FullDuplex) {
    return RtlGetLink(Up, Mbps, FullDuplex);
}

static const NIC_L2 gRtlNicL2 = {
    .SendFrame = RtlNicSendFrame,
    .Poll = RtlNicPoll,
    .GetMac = RtlNicGetMac,
    .GetLink = RtlNicGetLink,
};

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
    return NetAttachNic(&gRtlNicL2);
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
