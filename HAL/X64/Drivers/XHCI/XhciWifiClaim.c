/*
 * XhciWifiClaim.c — xHCI 上认 RTL8188EU（PR-N-wifi-1）
 *
 * VID 0BDA + PID 白名单；SetConfig；记 Bulk EP 供 wifi-2。不灌固件。
 */
#include "XHCI/XhciInternal.h"
#include "Wifi.h"

#define WIFI_CFG_MAX 512

UINT32 gWifiSlot;
UINT32 gWifiPort;
UINT32 gWifiRoute;
UINT8 gWifiHubSlot;
UINT8 gWifiTtPort;
UINT16 gWifiXhciPid;
UINT8 gWifiEpIn;
UINT8 gWifiEpOut;
UINT16 gWifiMpsIn;
UINT16 gWifiMpsOut;
int gWifiXhciOk;

UINT8 gWifiDevCtx[2048] __attribute__((aligned(64)));
XHCI_TRB gWifiEp0Ring[RING_SIZE] __attribute__((aligned(64)));
RING_STATE gWifiEp0;
static UINT8 gWifiCfgBuf[WIFI_CFG_MAX] __attribute__((aligned(64)));

void XhciWifiEp0Ring(XHCI_TRB **RingOut, RING_STATE **StOut) {
    *RingOut = gWifiEp0Ring;
    *StOut = &gWifiEp0;
}

int XhciWifiReady(void) {
    return (gWifiXhciOk && gWifiSlot != 0) ? 1 : 0;
}

UINT16 XhciWifiPid(void) {
    return gWifiXhciPid;
}

static int ParseWifiBulk(UINT8 *Cfg, UINT16 Total, UINT8 *EpIn, UINT16 *MpsIn,
                         UINT8 *EpOut, UINT16 *MpsOut) {
    UINT16 Off = 0;
    UINT8 CurAlt = 0;
    UINT8 BestIn = 0;
    UINT8 BestOut = 0;
    UINT16 BestInMps = 0;
    UINT16 BestOutMps = 0;

    while (Off + 2 <= Total) {
        UINT8 Len = Cfg[Off];
        UINT8 Type = Cfg[Off + 1];

        if (Len < 2 || Off + Len > Total) {
            break;
        }
        if (Type == 4 && Len >= 9) {
            CurAlt = Cfg[Off + 3];
        } else if (Type == 5 && Len >= 7 && CurAlt == 0) {
            UINT8 Addr = Cfg[Off + 2];
            UINT8 Attr = Cfg[Off + 3];
            UINT16 Mps = (UINT16)(Cfg[Off + 4] | (Cfg[Off + 5] << 8));

            if ((Attr & 0x03) == 2) {
                if (Addr & 0x80) {
                    BestIn = Addr;
                    BestInMps = Mps ? Mps : 64;
                } else {
                    BestOut = Addr;
                    BestOutMps = Mps ? Mps : 64;
                }
            }
        }
        Off = (UINT16)(Off + Len);
    }
    if (BestIn == 0 || BestOut == 0) {
        return 0;
    }
    if (EpIn) {
        *EpIn = BestIn;
    }
    if (MpsIn) {
        *MpsIn = BestInMps;
    }
    if (EpOut) {
        *EpOut = BestOut;
    }
    if (MpsOut) {
        *MpsOut = BestOutMps;
    }
    return 1;
}

int XhciWifiFinishClaim(UINT32 RootPort, UINT8 Speed) {
    UINT8 EpIn = 0;
    UINT8 EpOut = 0;
    UINT16 MpsIn = 64;
    UINT16 MpsOut = 64;
    UINT16 Total;
    UINT8 ConfigVal = 1;
    UINT16 Vid;
    UINT16 Pid;

    (void)Speed;
    if (gWifiSlot == 0) {
        return 0;
    }
    gXferSlot = gWifiSlot;
    if (GetDeviceDesc() < 0) {
        goto fail;
    }
    Vid = (UINT16)(gCtrlBuf[8] | (gCtrlBuf[9] << 8));
    Pid = (UINT16)(gCtrlBuf[10] | (gCtrlBuf[11] << 8));
    if (Vid != WIFI_RTL_VID || !WifiIs8188EuPid(Pid)) {
        goto fail;
    }
    if (GetDesc(0x0200, 0, 9, gWifiCfgBuf) < 0) {
        goto fail;
    }
    Total = (UINT16)(gWifiCfgBuf[2] | (gWifiCfgBuf[3] << 8));
    if (Total < 9) {
        Total = 9;
    }
    if (Total > sizeof(gWifiCfgBuf)) {
        Total = (UINT16)sizeof(gWifiCfgBuf);
    }
    ConfigVal = gWifiCfgBuf[5] ? gWifiCfgBuf[5] : 1;
    if (GetDesc(0x0200, 0, Total, gWifiCfgBuf) < 0) {
        goto fail;
    }
    if (!ParseWifiBulk(gWifiCfgBuf, Total, &EpIn, &MpsIn, &EpOut, &MpsOut)) {
        goto fail;
    }
    if (SetConfig(ConfigVal) < 0) {
        goto fail;
    }
    gWifiPort = RootPort;
    gWifiEpIn = EpIn;
    gWifiEpOut = EpOut;
    gWifiMpsIn = MpsIn;
    gWifiMpsOut = MpsOut;
    gWifiXhciPid = Pid;
    gWifiXhciOk = 1;
    return 1;

fail:
    if (gWifiSlot != 0) {
        DisableSlot(gWifiSlot);
        gWifiSlot = 0;
    }
    return 0;
}

static int WifiAddressRoot(UINT32 P, UINT8 *Speed) {
    UINT32 Ps;
    int Force = 0;

    Ps = ReadMmio32(gOperationalBase + PortReg(P));
    if (gMscClaimed) {
        if (!(Ps & PORTSC_CCS) || !(Ps & PORTSC_PED)) {
            return 0;
        }
    } else if (!MscClaimForceUntilPed(P, &Force)) {
        return 0;
    }
    Ps = ReadMmio32(gOperationalBase + PortReg(P));
    *Speed = PortSpeed(Ps);
    if (gWifiSlot != 0) {
        DisableSlot(gWifiSlot);
        gWifiSlot = 0;
    }
    gWifiRoute = 0;
    gWifiHubSlot = 0;
    gWifiTtPort = 0;
    if (!AddressDeviceOnPort(P, *Speed, &gWifiSlot, gWifiDevCtx, 0, 0, 0, 0, 0)) {
        if (gWifiSlot != 0) {
            DisableSlot(gWifiSlot);
            gWifiSlot = 0;
        }
        return 0;
    }
    if (gWifiSlot <= DCBAA_SLOTS) {
        gSlotEp0UsesKbdRing[gWifiSlot] = 0;
    }
    return 1;
}

int XhciWifiTryHubChildren(void);

int XhciWifiClaim(void) {
    UINT32 P;
    UINT8 Speed;

    if (!gXhciStarted || gOperationalBase == 0 || gMaxPorts == 0) {
        return -1;
    }
    if (gWifiXhciOk && gWifiSlot != 0) {
        return 1;
    }
    /*
     * MSC 已认则禁止再扫口/Address：会踩 hub 上 U 盘（cc=0x04 bulk fail）。
     * 主路径已改 iwl PCIe；USB 棒仅无 MSC 时才试。
     */
    if (gMscClaimed) {
        return 0;
    }
    if (gHubSlotId != 0 && XhciWifiTryHubChildren()) {
        goto armed;
    }
    for (P = 1; P <= gMaxPorts && P <= 32u; P++) {
        UINT32 Ps = ReadMmio32(gOperationalBase + PortReg(P));

        if (!(Ps & PORTSC_CCS)) {
            continue;
        }
        if (gSlotId != 0 && P == gPort1) {
            continue;
        }
        if (gMouseSlotId != 0 && P == gMousePort) {
            continue;
        }
        if (gHubSlotId != 0 && P == gHubRootPort) {
            continue;
        }
        if (gMscClaimed && gMscPort == P) {
            continue;
        }
        if (gFtdiClaimed && gFtdiPort == P) {
            continue;
        }
        if (!WifiAddressRoot(P, &Speed)) {
            continue;
        }
        if (XhciWifiFinishClaim(P, Speed)) {
            goto armed;
        }
    }
    return 0;

armed:
    if (gSlotId != 0 && gIntrDci != 0) {
        QueueIntr();
    }
    if (gMouseSlotId != 0 && gMouseIntrDci != 0) {
        QueueMouseIntr();
    }
    return 1;
}
