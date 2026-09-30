/*
 * NetAlx.c — alx 经 Driver Net 类注册（PR-N-alx-2）
 *
 * Probe 认 PCI 1969:1091…；Bind → NetAttachNic(NIC_L2)。
 */
#include "Driver.h"
#include "DriverNic.h"
#include "Alx.h"
#include "Net.h"
#include "VirtualMemory.h"

static int AlxNicSendFrame(const UINT8 *Frame, UINTN FrameLen) {
    return AlxSendFrame(Frame, FrameLen);
}

static void AlxNicPoll(void) {
    AlxPoll();
}

static void AlxNicGetMac(UINT8 Mac[6]) {
    AlxGetMac(Mac);
}

static int AlxNicGetLink(int *Up, UINT32 *Mbps, int *FullDuplex) {
    return AlxGetLink(Up, Mbps, FullDuplex);
}

static const NIC_L2 gAlxNicL2 = {
    .SendFrame = AlxNicSendFrame,
    .Poll = AlxNicPoll,
    .GetMac = AlxNicGetMac,
    .GetLink = AlxNicGetLink,
};

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
    return NetAttachNic(&gAlxNicL2);
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
