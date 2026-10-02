/*
 * XhciWifiHub.c — 已有 HID hub 时扫子口认 8188EU（PR-N-wifi-1）
 */
#include "XHCI/XhciInternal.h"

extern UINT32 gWifiRoute;
extern UINT8 gWifiHubSlot;
extern UINT8 gWifiTtPort;
extern UINT8 gWifiDevCtx[2048];
extern UINT32 gWifiSlot;
extern int gWifiXhciOk;

int XhciWifiFinishClaim(UINT32 RootPort, UINT8 Speed);

int XhciWifiTryHubChildren(void) {
    UINT8 Port;
    UINT8 MaxP = gHubNumPorts;
    UINT32 St;
    UINT8 Speed;

    if (gHubSlotId == 0 || gWifiXhciOk) {
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
        if (gWifiSlot != 0) {
            DisableSlot(gWifiSlot);
            gWifiSlot = 0;
        }
        gWifiRoute = (UINT32)Port;
        gWifiHubSlot = (UINT8)gHubSlotId;
        gWifiTtPort = Port;
        if (!AddressDeviceOnPort(gHubRootPort, Speed, &gWifiSlot, gWifiDevCtx,
                                 (UINT32)Port, (UINT8)gHubSlotId, Port, 0, 0)) {
            if (gWifiSlot != 0) {
                DisableSlot(gWifiSlot);
                gWifiSlot = 0;
            }
            continue;
        }
        if (gWifiSlot <= DCBAA_SLOTS) {
            gSlotEp0UsesKbdRing[gWifiSlot] = 0;
        }
        if (XhciWifiFinishClaim(gHubRootPort, Speed)) {
            return 1;
        }
    }
    return 0;
}
