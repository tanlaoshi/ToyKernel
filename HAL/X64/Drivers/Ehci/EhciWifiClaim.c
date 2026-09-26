/*
 * EhciWifiClaim.c — EHCI 上认 RTL8188EU（PR-N-wifi-1）
 *
 * 扫 RMH 子口；VID 0BDA + PID 白名单；SetConfig；记 Bulk EP。
 */
#include "EhciPrivate.h"
#include "Wifi.h"
#include "ToySerialLog.h"

#define HUB_FEAT_PORT_POWER 8u
#define HUB_STAT_CONNECT    (1u << 0)

UINT16 gEhciWifiPid;
EHCI_CTRL *gEhciWifiCtrl;
UINT8 gEhciWifiAddr;
UINT8 gEhciWifiSpeed;
UINT8 gEhciWifiHubAddr;
UINT8 gEhciWifiHubPort;
UINT8 gEhciWifiEpIn;
UINT8 gEhciWifiEpOut;
UINT16 gEhciWifiMpsIn;
UINT16 gEhciWifiMpsOut;
UINT8 gEhciWifiEpMax0;
int gEhciWifiOk;

int EhciWifiReady(void) {
    return (gEhciWifiOk && gEhciWifiCtrl) ? 1 : 0;
}

UINT16 EhciWifiPid(void) {
    return gEhciWifiPid;
}

static int GetDesc(EHCI_CTRL *C, UINT8 Addr, UINT8 EpMax, UINT16 TypeIndex,
                   UINT16 Len, void *Out) {
    USB_SETUP_PACKET S;

    S.bmRequestType = 0x80;
    S.bRequest = 0x06;
    S.wValue = TypeIndex;
    S.wIndex = 0;
    S.wLength = Len;
    return EhciControlXfer(C, Addr, EpMax, &S, Out);
}

static int SetAddr(EHCI_CTRL *C, UINT8 NewAddr, UINT8 EpMax) {
    USB_SETUP_PACKET S;

    S.bmRequestType = 0x00;
    S.bRequest = 0x05;
    S.wValue = NewAddr;
    S.wIndex = 0;
    S.wLength = 0;
    return EhciControlXfer(C, 0, EpMax, &S, 0);
}

static int SetConfig(EHCI_CTRL *C, UINT8 Addr, UINT8 EpMax, UINT8 Cfg) {
    USB_SETUP_PACKET S;

    S.bmRequestType = 0x00;
    S.bRequest = 0x09;
    S.wValue = Cfg;
    S.wIndex = 0;
    S.wLength = 0;
    return EhciControlXfer(C, Addr, EpMax, &S, 0);
}

static int ParseWifiBulk(UINT8 *Cfg, UINT16 Total, UINT8 *EpIn, UINT16 *MpsIn,
                         UINT8 *EpOut, UINT16 *MpsOut, UINT8 *CfgVal) {
    UINT16 Off = 0;
    UINT8 CurAlt = 0;
    UINT8 BestIn = 0;
    UINT8 BestOut = 0;
    UINT16 BestInMps = 0;
    UINT16 BestOutMps = 0;

    *CfgVal = 1;
    while (Off + 2 <= Total) {
        UINT8 Len = Cfg[Off];
        UINT8 Type = Cfg[Off + 1];

        if (Len < 2 || Off + Len > Total) {
            break;
        }
        if (Type == 0x02 && Len >= 9) {
            *CfgVal = Cfg[Off + 5] ? Cfg[Off + 5] : 1;
        } else if (Type == 4 && Len >= 9) {
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
    *EpIn = BestIn;
    *MpsIn = BestInMps;
    *EpOut = BestOut;
    *MpsOut = BestOutMps;
    return 1;
}

static int FinishWifi(EHCI_CTRL *C, UINT8 Speed, UINT8 HubAddr, UINT8 HubPort) {
    USB_DEVICE_DESCRIPTOR Dev;
    UINT8 Cfg[256];
    UINT8 EpMax = 8;
    UINT8 Addr;
    UINT8 EpIn;
    UINT8 EpOut;
    UINT16 MpsIn;
    UINT16 MpsOut;
    UINT8 CfgVal;
    UINT16 CfgLen;

    C->XferSpeed = Speed;
    C->XferHubAddr = HubAddr;
    C->XferHubPort = HubPort;

    if (GetDesc(C, 0, 8, 0x0100, 8, &Dev) != 0) {
        return 0;
    }
    EpMax = Dev.bMaxPacketSize0 ? Dev.bMaxPacketSize0 : 8;
    if (GetDesc(C, 0, EpMax, 0x0100, 18, &Dev) != 0) {
        return 0;
    }
    if (Dev.idVendor != WIFI_RTL_VID || !WifiIs8188EuPid(Dev.idProduct)) {
        return 0;
    }
    if (gEhciNextAddr < 2 || gEhciNextAddr > 127) {
        gEhciNextAddr = 2;
    }
    Addr = gEhciNextAddr++;
    if (SetAddr(C, Addr, EpMax) != 0) {
        return 0;
    }
    C->XferSpeed = Speed;
    C->XferHubAddr = HubAddr;
    C->XferHubPort = HubPort;
    EhciDelay(100000);
    if (GetDesc(C, Addr, EpMax, 0x0200, 9, Cfg) != 0) {
        return 0;
    }
    CfgLen = (UINT16)(Cfg[2] | ((UINT16)Cfg[3] << 8));
    if (CfgLen < 9 || CfgLen > sizeof(Cfg)) {
        CfgLen = 64;
    }
    if (GetDesc(C, Addr, EpMax, 0x0200, CfgLen, Cfg) != 0) {
        return 0;
    }
    if (!ParseWifiBulk(Cfg, CfgLen, &EpIn, &MpsIn, &EpOut, &MpsOut, &CfgVal)) {
        return 0;
    }
    if (SetConfig(C, Addr, EpMax, CfgVal) != 0) {
        return 0;
    }

    gEhciWifiCtrl = C;
    gEhciWifiAddr = Addr;
    gEhciWifiSpeed = Speed;
    gEhciWifiHubAddr = HubAddr;
    gEhciWifiHubPort = HubPort;
    gEhciWifiEpIn = EpIn;
    gEhciWifiEpOut = EpOut;
    gEhciWifiMpsIn = MpsIn ? MpsIn : 64;
    gEhciWifiMpsOut = MpsOut ? MpsOut : 64;
    gEhciWifiEpMax0 = EpMax;
    gEhciWifiPid = Dev.idProduct;
    gEhciWifiOk = 1;
    return 1;
}

static int TryHubPort(EHCI_CTRL *C, UINT8 HubAddr, UINT8 P) {
    UINT16 St;
    UINT16 Ch;
    UINT8 Sp;

    if (C->HidOk && C->HubAddr == HubAddr && C->HubPort == P) {
        return 0;
    }
    if (C->MscOk && C->MscHubAddr == HubAddr && C->MscHubPort == P) {
        return 0;
    }
    if (gEhciFtdiOk && gEhciFtdiHubAddr == HubAddr && gEhciFtdiHubPort == P) {
        return 0;
    }
    if (!EhciMscHubGetStatus(C, HubAddr, P, &St, &Ch)) {
        return 0;
    }
    if (Ch & HUB_STAT_CONNECT) {
        (void)EhciMscHubClearFeat(C, HubAddr, P, 16u);
    }
    if ((St & HUB_STAT_CONNECT) == 0) {
        return 0;
    }
    if (!EhciMscHubResetPort(C, HubAddr, P, &Sp)) {
        return 0;
    }
    return FinishWifi(C, Sp, HubAddr, P);
}

int EhciWifiClaimViaHub(EHCI_CTRL *C) {
    UINT8 HubAddr = C->HubAddr;
    UINT8 Desc[16];
    UINT8 NPorts = 6;
    UINT8 P;

    if (!HubAddr) {
        return 0;
    }
    if (EhciMscHubGetDesc(C, HubAddr, Desc, 9)) {
        NPorts = Desc[2] ? Desc[2] : 6;
        if (NPorts > 14) {
            NPorts = 14;
        }
    }
    for (P = 1; P <= NPorts; P++) {
        (void)EhciMscHubSetFeat(C, HubAddr, P, HUB_FEAT_PORT_POWER);
    }
    EhciDelay(500000);
    for (P = 1; P <= NPorts; P++) {
        if (TryHubPort(C, HubAddr, P)) {
            return 1;
        }
    }
    return 0;
}

int EhciWifiClaim(void) {
    int i;

    if (!gEhciReady) {
        return -1;
    }
    if (EhciWifiReady()) {
        return 1;
    }
    for (i = 0; i < gEhciCount; i++) {
        EHCI_CTRL *C = &gEhci[i];

        if (!C->Up || !C->Sched) {
            continue;
        }
        if (C->HubAddr && EhciWifiClaimViaHub(C)) {
            return 1;
        }
    }
    return 0;
}
