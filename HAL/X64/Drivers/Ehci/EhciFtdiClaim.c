/*
 * EhciFtdiClaim.c — EHCI 上认 FT232（PR-H-ehci-4）
 *
 * 扫 RMH 子口；VID 0403 / PID 6001·6014·6015；SetConfig + 115200。
 */
#include "EhciPrivate.h"
#include "ToySerialLog.h"
#include "Hal.h"

#define FTDI_VID         0x0403u
#define FTDI_PID_FT232R  0x6001u
#define FTDI_PID_FT232H  0x6014u
#define FTDI_PID_FT231X  0x6015u
#define FTDI_REQ_RESET      0x00u
#define FTDI_REQ_MODEM_CTRL 0x01u
#define FTDI_REQ_SET_FLOW   0x02u
#define FTDI_REQ_SET_BAUD   0x03u
#define FTDI_REQ_SET_DATA   0x04u
#define FTDI_BAUD_115200    0x001Au
#define FTDI_DTR_RTS_HIGH   0x0303u
#define HUB_FEAT_PORT_POWER 8u
#define HUB_STAT_CONNECT    (1u << 0)

EHCI_CTRL *gEhciFtdiCtrl;
UINT8 gEhciFtdiAddr;
UINT8 gEhciFtdiSpeed;
UINT8 gEhciFtdiHubAddr;
UINT8 gEhciFtdiHubPort;
UINT8 gEhciFtdiEpIn;
UINT8 gEhciFtdiEpOut;
UINT16 gEhciFtdiMpsIn;
UINT16 gEhciFtdiMpsOut;
UINT8 gEhciFtdiDtIn;
UINT8 gEhciFtdiDtOut;
UINT8 gEhciFtdiEpMax0;
int gEhciFtdiOk;

static int IsFtdiPid(UINT16 Pid) {
    return Pid == FTDI_PID_FT232R || Pid == FTDI_PID_FT232H ||
           Pid == FTDI_PID_FT231X;
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

static int FtdiVendor(EHCI_CTRL *C, UINT8 Req, UINT16 Value, UINT16 Index) {
    USB_SETUP_PACKET S;

    S.bmRequestType = 0x40;
    S.bRequest = Req;
    S.wValue = Value;
    S.wIndex = Index;
    S.wLength = 0;
    C->XferSpeed = gEhciFtdiSpeed;
    C->XferHubAddr = gEhciFtdiHubAddr;
    C->XferHubPort = gEhciFtdiHubPort;
    return EhciControlXfer(C, gEhciFtdiAddr, gEhciFtdiEpMax0 ? gEhciFtdiEpMax0 : 8,
                           &S, 0);
}

static int ParseFtdiBulk(UINT8 *Cfg, UINT16 Total, UINT8 *EpIn, UINT16 *MpsIn,
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

static int FinishFtdi(EHCI_CTRL *C, UINT8 Speed, UINT8 HubAddr, UINT8 HubPort) {
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
        ToyBootMarkUsb("Boot: EHCI FTDI getdesc8 fail\n");
        return 0;
    }
    EpMax = Dev.bMaxPacketSize0 ? Dev.bMaxPacketSize0 : 8;
    if (GetDesc(C, 0, EpMax, 0x0100, 18, &Dev) != 0) {
        ToyBootMarkUsb("Boot: EHCI FTDI getdesc18 fail\n");
        return 0;
    }
    if (Dev.idVendor != FTDI_VID || !IsFtdiPid(Dev.idProduct)) {
        return 0; /* 非 FTDI：静默换口 */
    }
    ToyBootMarkUsb("Boot: EHCI FTDI vid=0403 found\n");
    if (gEhciNextAddr < 2 || gEhciNextAddr > 127) {
        gEhciNextAddr = 2;
    }
    Addr = gEhciNextAddr++;
    if (SetAddr(C, Addr, EpMax) != 0) {
        ToyBootMarkUsb("Boot: EHCI FTDI setaddr fail\n");
        return 0;
    }
    C->XferSpeed = Speed;
    C->XferHubAddr = HubAddr;
    C->XferHubPort = HubPort;
    EhciDelay(100000);
    if (GetDesc(C, Addr, EpMax, 0x0200, 9, Cfg) != 0) {
        ToyBootMarkUsb("Boot: EHCI FTDI cfg9 fail\n");
        return 0;
    }
    CfgLen = (UINT16)(Cfg[2] | ((UINT16)Cfg[3] << 8));
    if (CfgLen < 9 || CfgLen > sizeof(Cfg)) {
        CfgLen = 64;
    }
    if (GetDesc(C, Addr, EpMax, 0x0200, CfgLen, Cfg) != 0) {
        ToyBootMarkUsb("Boot: EHCI FTDI cfg fail\n");
        return 0;
    }
    if (!ParseFtdiBulk(Cfg, CfgLen, &EpIn, &MpsIn, &EpOut, &MpsOut, &CfgVal)) {
        ToyBootMarkUsb("Boot: EHCI FTDI no bulk\n");
        return 0;
    }
    if (SetConfig(C, Addr, EpMax, CfgVal) != 0) {
        ToyBootMarkUsb("Boot: EHCI FTDI setcfg fail\n");
        return 0;
    }

    gEhciFtdiCtrl = C;
    gEhciFtdiAddr = Addr;
    gEhciFtdiSpeed = Speed;
    gEhciFtdiHubAddr = HubAddr;
    gEhciFtdiHubPort = HubPort;
    gEhciFtdiEpIn = EpIn;
    gEhciFtdiEpOut = EpOut;
    gEhciFtdiMpsIn = MpsIn ? MpsIn : 64;
    gEhciFtdiMpsOut = MpsOut ? MpsOut : 64;
    gEhciFtdiDtIn = 0;
    gEhciFtdiDtOut = 0;
    gEhciFtdiEpMax0 = EpMax;
    gEhciFtdiOk = 0; /* baud 成功后再置 */

    if (FtdiVendor(C, FTDI_REQ_RESET, 0, 0) != 0 ||
        FtdiVendor(C, FTDI_REQ_SET_BAUD, FTDI_BAUD_115200, 0) != 0 ||
        FtdiVendor(C, FTDI_REQ_SET_DATA, 8, 0) != 0) {
        ToyBootMarkUsb("Boot: EHCI FTDI baud fail\n");
        gEhciFtdiCtrl = 0;
        return 0;
    }
    (void)FtdiVendor(C, FTDI_REQ_SET_FLOW, 0, 0);
    (void)FtdiVendor(C, FTDI_REQ_MODEM_CTRL, FTDI_DTR_RTS_HIGH, 0);
    gEhciFtdiOk = 1;
    {
        char Probe[] = "ftdi-tx\r\n";
        gEhciFtdiDtOut = 0;
        if (EhciBulkXfer(C, gEhciFtdiAddr, gEhciFtdiEpOut, gEhciFtdiMpsOut,
                         gEhciFtdiSpeed, gEhciFtdiHubAddr, gEhciFtdiHubPort, 0,
                         Probe, 8, &gEhciFtdiDtOut) < 0) {
            ToyBootMarkUsb("Boot: EHCI FTDI tx fail ");
            ToyBootMarkUsb(gEhciLastErr ? gEhciLastErr : "?");
            ToyBootMarkUsb("\n");
        } else {
            ToyBootMarkUsb("Boot: EHCI FTDI tx ok\n");
        }
    }
    ToyLogBoot("boot: usb-uart ftdi\n");
    ToyBootMarkUsb("boot: usb-uart ftdi\n");
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
    if (!EhciMscHubGetStatus(C, HubAddr, P, &St, &Ch)) {
        return 0;
    }
    if (Ch & HUB_STAT_CONNECT) {
        (void)EhciMscHubClearFeat(C, HubAddr, P, 16u); /* C_PORT_CONNECTION */
    }
    if ((St & HUB_STAT_CONNECT) == 0) {
        return 0;
    }
    if (!EhciMscHubResetPort(C, HubAddr, P, &Sp)) {
        ToyBootMarkUsb("Boot: EHCI FTDI hub reset fail\n");
        return 0;
    }
    ToyBootMarkUsb(Sp == EHCI_SPEED_HS ? "Boot: EHCI FTDI hub spd=HS\n"
                    : (Sp == EHCI_SPEED_LS ? "Boot: EHCI FTDI hub spd=LS\n"
                                          : "Boot: EHCI FTDI hub spd=FS\n"));
    return FinishFtdi(C, Sp, HubAddr, P);
}

int EhciFtdiClaimViaHub(EHCI_CTRL *C) {
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
