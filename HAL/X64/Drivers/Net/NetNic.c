/*
 * NetNic.c — NetAttachNic / 双槽 L2 + 有线优先（PR-N-nic-2slot）
 *
 * 胖 NET_BACKEND 仍由 Net.c 提供；外置 NIC 只挂 NIC_L2。
 * 槽 0=有线、1=无线；出站/MAC/链路跟当前主槽；Poll 扫两槽。
 */
#include "NetPrivate.h"
#include "Net.h"
#include "DriverNic.h"
#include "Debug.h"
#include "NetConfig.h"

#define NET_NIC_SLOTS 2

const NIC_L2 *gNicL2;
static const NIC_L2 *gNicSlot[NET_NIC_SLOTS];
static NET_NIC_KIND gPrimaryKind = NET_NIC_KIND_WIRED;
static UINT32 gNicEpoch;
static int gProtoAttached;

static int SlotLinkUp(const NIC_L2 *Nic) {
    int Up = 0;

    if (!Nic) {
        return 0;
    }
    if (!Nic->GetLink) {
        return 1;
    }
    if (Nic->GetLink(&Up, 0, 0) != 0) {
        return 0;
    }
    return Up ? 1 : 0;
}

static void ApplyPrimary(NET_NIC_KIND Kind) {
    const NIC_L2 *Nic = gNicSlot[(int)Kind];

    gPrimaryKind = Kind;
    if (gNicL2 == Nic) {
        return;
    }
    gNicL2 = Nic;
    if (!Nic) {
        return;
    }
    gNicEpoch++;
    Nic->GetMac(gMac);
    gLwIpRx = 0;
    gNetOk = 1;
    NetConfigEnsure();
    DebugWrite(Kind == NET_NIC_KIND_WIFI ? "Net: primary wifi\n" : "Net: primary wired\n");
}

static void SelectPrimary(void) {
    int WiredUp = SlotLinkUp(gNicSlot[NET_NIC_KIND_WIRED]);
    int WifiUp = SlotLinkUp(gNicSlot[NET_NIC_KIND_WIFI]);

    if (WiredUp) {
        ApplyPrimary(NET_NIC_KIND_WIRED);
        return;
    }
    if (WifiUp) {
        ApplyPrimary(NET_NIC_KIND_WIFI);
        return;
    }
    if (gNicSlot[NET_NIC_KIND_WIRED]) {
        ApplyPrimary(NET_NIC_KIND_WIRED);
        return;
    }
    if (gNicSlot[NET_NIC_KIND_WIFI]) {
        ApplyPrimary(NET_NIC_KIND_WIFI);
        return;
    }
    gNicL2 = 0;
}

int NetAttachNicKind(const NIC_L2 *Nic, NET_NIC_KIND Kind) {
    int Slot;

    if (!NicL2OpsValid(Nic)) {
        DebugWrite("Net: bad NIC_L2\n");
        return -1;
    }
    Slot = (int)Kind;
    if (Slot < 0 || Slot >= NET_NIC_SLOTS) {
        return -1;
    }
    gNicSlot[Slot] = Nic;
    SelectPrimary();
    if (!gProtoAttached && gNicL2) {
        gProtoAttached = 1;
        DebugWrite("Net: NIC_L2 attached\n");
        return NetProtocolAttach();
    }
    DebugWrite("Net: NIC_L2 slot set\n");
    return 0;
}

int NetAttachNic(const NIC_L2 *Nic) {
    return NetAttachNicKind(Nic, NET_NIC_KIND_WIRED);
}

int NetNicSendFrame(const UINT8 *Frame, UINTN FrameLen) {
    int Result;

    if (!gNicL2 || !gNicL2->SendFrame) {
        return -2;
    }
    Result = gNicL2->SendFrame(Frame, FrameLen);
    if (Result == 0) {
        gTxDone++;
    }
    return Result;
}

void NetNicPoll(void) {
    int i;

    for (i = 0; i < NET_NIC_SLOTS; i++) {
        if (gNicSlot[i] && gNicSlot[i]->Poll) {
            gNicSlot[i]->Poll();
        }
    }
    SelectPrimary();
}

int NetNicHasL2(void) {
    return gNicL2 != 0 ? 1 : 0;
}

UINT32 NetNicEpoch(void) {
    return gNicEpoch;
}

int NetNicGetLink(int *Up, UINT32 *Mbps, int *FullDuplex) {
    if (!gNicL2 || !gNicL2->GetLink) {
        return -1;
    }
    return gNicL2->GetLink(Up, Mbps, FullDuplex);
}

int NetNicPrimaryKind(void) {
    if (!gNicL2) {
        return -1;
    }
    return (int)gPrimaryKind;
}
