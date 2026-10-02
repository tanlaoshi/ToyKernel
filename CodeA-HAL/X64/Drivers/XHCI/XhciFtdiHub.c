/*
 * XhciFtdiHub.c — PR-H-usb-uart-ftdi-1：已有 HID hub 时扫子口认 FT232
 */
#include "XHCI/XhciInternal.h"

extern UINT32 gFtdiRoute;
extern UINT8 gFtdiHubSlot;
extern UINT8 gFtdiTtPort;
extern UINT8 gFtdiDevCtx[2048];

int XhciFtdiFinishClaim(UINT32 RootPort, UINT8 Speed);

int XhciFtdiTryHubChildren(void) {
    UINT8 Port;
    UINT8 MaxP = gHubNumPorts;
    UINT32 St;
    UINT8 Speed;

    if (gHubSlotId == 0 || gFtdiClaimed) {
        return 0;
    }
    if (MaxP == 0 || MaxP > 15) {
        MaxP = 8;
    }
    for (Port = 1; Port <= MaxP; Port++) {
        (void)HubSetPortFeat(Port, HUB_FEAT_PORT_POWER);
    }
    StallMs(HalCpuIsHypervisor() ? 20 : 200);
    for (Port = 1; Port <= MaxP; Port++) {
        int t;

        if (HubGetPortStatus(Port, &St) < 0 || !(St & HUB_PORT_CONNECTION)) {
            continue;
        }
        if ((gKbdRoute & 0xF) == (UINT32)Port && gSlotId != 0) {
            continue;
        }
        if ((gMouseRoute & 0xF) == (UINT32)Port && gMouseSlotId != 0) {
            continue;
        }
        if (HubSetPortFeat(Port, HUB_FEAT_PORT_RESET) < 0) {
            continue;
        }
        for (t = 0; t < 80; t++) {
            if (HubGetPortStatus(Port, &St) < 0) {
                break;
            }
            if (St & HUB_C_PORT_RESET) {
                (void)HubClearPortFeat(Port, HUB_FEAT_C_PORT_RESET);
                break;
            }
            StallMs(5);
        }
        for (t = 0; t < 80; t++) {
            if (HubGetPortStatus(Port, &St) < 0) {
                break;
            }
            if (St & HUB_PORT_ENABLE) {
                break;
            }
            StallMs(5);
        }
        if (!(St & HUB_PORT_ENABLE)) {
            continue;
        }
        Speed = HubPortSpeed(St);
        if (gFtdiSlot != 0) {
            DisableSlot(gFtdiSlot);
            gFtdiSlot = 0;
        }
        gFtdiRoute = (UINT32)Port;
        gFtdiHubSlot = (UINT8)gHubSlotId;
        gFtdiTtPort = Port;
        if (!AddressDeviceOnPort(gHubRootPort, Speed, &gFtdiSlot, gFtdiDevCtx,
                                 (UINT32)Port, (UINT8)gHubSlotId, Port, 0, 0)) {
            if (gFtdiSlot != 0) {
                DisableSlot(gFtdiSlot);
                gFtdiSlot = 0;
            }
            continue;
        }
        if (gFtdiSlot <= DCBAA_SLOTS) {
            gSlotEp0UsesKbdRing[gFtdiSlot] = 0;
        }
        if (XhciFtdiFinishClaim(gHubRootPort, Speed)) {
            return 1;
        }
    }
    return 0;
}
