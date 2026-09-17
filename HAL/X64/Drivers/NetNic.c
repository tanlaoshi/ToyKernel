/*
 * NetNic.c — NetAttachNic / L2 分发（PR-N-nic-attach）
 *
 * 胖 NET_BACKEND 仍由 Net.c 提供；外置 NIC 只挂 NIC_L2。
 */
#include "NetPriv.h"
#include "Net.h"
#include "DriverNic.h"
#include "Debug.h"

const NIC_L2 *gNicL2;

int NetAttachNic(const NIC_L2 *Nic) {
    if (!NicL2OpsValid(Nic)) {
        DebugWrite("Net: bad NIC_L2\n");
        return -1;
    }
    gNicL2 = Nic;
    Nic->GetMac(gMac);
    gLwIpRx = 0;
    gNetOk = 1;
    DebugWrite("Net: NIC_L2 attached\n");
    return NetProtocolAttach();
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
    if (gNicL2 && gNicL2->Poll) {
        gNicL2->Poll();
    }
}

int NetNicHasL2(void) {
    return gNicL2 != 0 ? 1 : 0;
}

int NetNicGetLink(int *Up, UINT32 *Mbps, int *FullDuplex) {
    if (!gNicL2 || !gNicL2->GetLink) {
        return -1;
    }
    return gNicL2->GetLink(Up, Mbps, FullDuplex);
}
