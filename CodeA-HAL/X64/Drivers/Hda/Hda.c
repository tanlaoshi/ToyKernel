/*
 * Hda.c — PR-G-audio-0：Intel HD Audio PCI 认卡（NUC7 / QEMU intel-hda）
 *
 * 只认卡 + lsdev；不映 BAR、不碰 CORB。无卡软退不挡桌面。
 */
#include "Hda.h"
#include "Driver.h"
#include "Device.h"
#include "ToySerialLog.h"

static int gHdaProbed;
static UINT16 gHdaDid;
static UINT8 gHdaBus;
static UINT8 gHdaDev;
static UINT8 gHdaFn;
static DEVICE_NODE *gHdaNode;

int HdaProbed(void) {
    return gHdaProbed;
}

UINT16 HdaPciDid(void) {
    return gHdaDid;
}

UINT8 HdaPciBus(void) {
    return gHdaBus;
}

UINT8 HdaPciDev(void) {
    return gHdaDev;
}

UINT8 HdaPciFn(void) {
    return gHdaFn;
}

/*
 * Vendor 8086 + Class 04 (Multimedia) + Subclass 03 (HD Audio).
 * 不依赖 DID 白名单；串口打出实 DID 供 audio-1+ 钉表。
 */
static DEVICE_NODE *FindIntelHda(void) {
    int Idx;

    for (Idx = 0; Idx < DeviceCount(); Idx++) {
        DEVICE_NODE *Dev = DeviceGet(Idx);

        if (!Dev || Dev->Bound || Dev->Bus != DEVICE_BUS_PCI) {
            continue;
        }
        if (Dev->Vendor != 0x8086u) {
            continue;
        }
        if (Dev->Class != 0x04u || Dev->Subclass != 0x03u) {
            continue;
        }
        return Dev;
    }
    return 0;
}

static int HdaProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPrivate) {
    DEVICE_NODE *Dev;

    (void)Self;
    (void)BusCtx;
    if (OutPrivate) {
        *OutPrivate = 0;
    }

    Dev = FindIntelHda();
    if (!Dev) {
        ToyLogBoot("Boot: hda skip (no Intel HDA)\n");
        return -1;
    }

    gHdaNode = Dev;
    gHdaDid = Dev->Device;
    gHdaBus = Dev->PciBus;
    gHdaDev = Dev->PciDev;
    gHdaFn = Dev->PciFn;
    gHdaProbed = 1;

    ToyLogBoot("Boot: hda probe did=");
    ToyLogBootHex32((UINT32)gHdaDid);
    ToyLogBoot(" @");
    ToyLogBootHex32((UINT32)gHdaBus);
    ToyLogBoot(":");
    ToyLogBootHex32((UINT32)gHdaDev);
    ToyLogBoot(".");
    ToyLogBootHex32((UINT32)gHdaFn);
    ToyLogBoot("\n");
    return 0;
}

static int HdaBind(TOY_DRIVER_INSTANCE *Inst) {
    if (!Inst || !Inst->Driver || !gHdaNode) {
        return -1;
    }
    DeviceBindDriver(gHdaNode, Inst->Driver, Inst);
    ToyLogBoot("Boot: hda bound\n");
    return 0;
}

static void HdaRemove(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
    gHdaProbed = 0;
    gHdaNode = 0;
    gHdaDid = 0;
}

static const TOY_DRIVER gHdaDriver = {
    .Name = "hda",
    .Class = TOY_DRIVER_CLASS_AUDIO,
    .Match = 0,
    .Probe = HdaProbe,
    .Bind = HdaBind,
    .Remove = HdaRemove,
};

void HdaDriverRegister(void) {
    if (ToyDriverRegister(&gHdaDriver) != 0) {
        ToyLogBoot("Boot: hda register fail\n");
        return;
    }
    (void)ToyDriverProbeClass(TOY_DRIVER_CLASS_AUDIO);
}
