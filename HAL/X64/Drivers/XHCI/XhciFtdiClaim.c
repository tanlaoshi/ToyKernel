/*
 * XhciFtdiClaim.c — PR-H-usb-uart-ftdi-1：FT232 认领（VID/PID + Bulk OUT + 115200）
 */
#include "XHCI/XhciInternal.h"

#define FTDI_VID           0x0403u
#define FTDI_PID_FT232R    0x6001u
#define FTDI_PID_FT232H    0x6014u
#define FTDI_PID_FT231X    0x6015u
#define FTDI_REQ_RESET     0x00u
#define FTDI_REQ_SET_FLOW  0x02u
#define FTDI_REQ_SET_BAUD  0x03u
#define FTDI_REQ_SET_DATA  0x04u
#define FTDI_BAUD_115200   0x001Au

UINT32 gFtdiSlot;
UINT32 gFtdiPort;
UINT32 gFtdiRoute;
UINT8 gFtdiHubSlot;
UINT8 gFtdiTtPort;
UINT32 gFtdiBulkOutDci;
UINT16 gFtdiBulkOutMps;
int gFtdiClaimed;

UINT8 gFtdiDevCtx[2048] __attribute__((aligned(64)));
XHCI_TRB gFtdiEp0Ring[RING_SIZE] __attribute__((aligned(64)));
RING_STATE gFtdiEp0;
XHCI_TRB gFtdiBulkOutRing[RING_SIZE] __attribute__((aligned(64)));
RING_STATE gFtdiBulkOut;
static UINT8 gFtdiCfgBuf[512] __attribute__((aligned(64)));

void XhciFtdiEp0Ring(XHCI_TRB **RingOut, RING_STATE **StOut) {
    *RingOut = gFtdiEp0Ring;
    *StOut = &gFtdiEp0;
}

int XhciFtdiReady(void) {
    return (gFtdiClaimed && gFtdiSlot != 0) ? 1 : 0;
}

static int IsFtdiPid(UINT16 Pid) {
    return Pid == FTDI_PID_FT232R || Pid == FTDI_PID_FT232H || Pid == FTDI_PID_FT231X;
}

static int ParseFtdiBulk(UINT8 *Cfg, UINT16 Total, UINT8 *EpIn, UINT16 *MpsIn,
                         UINT8 *EpOut, UINT16 *MpsOut) {
    UINT16 Off = 0;
    UINT8 CurIface = 0xFF;
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
            CurIface = Cfg[Off + 2];
            CurAlt = Cfg[Off + 3];
        } else if (Type == 5 && Len >= 7 && CurIface != 0xFF && CurAlt == 0) {
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

static int FtdiVendor(UINT8 Req, UINT16 Value, UINT16 Index) {
    USB_SETUP_PACKET Setup;

    Setup.bmRequestType = 0x40;
    Setup.bRequest = Req;
    Setup.wValue = Value;
    Setup.wIndex = Index;
    Setup.wLength = 0;
    return ControlXfer(&Setup, 0);
}

static int FtdiSetLine115200(void) {
    if (FtdiVendor(FTDI_REQ_RESET, 0, 0) < 0) {
        return -1;
    }
    if (FtdiVendor(FTDI_REQ_SET_BAUD, FTDI_BAUD_115200, 0) < 0) {
        return -1;
    }
    if (FtdiVendor(FTDI_REQ_SET_DATA, 8, 0) < 0) {
        return -1;
    }
    (void)FtdiVendor(FTDI_REQ_SET_FLOW, 0, 0);
    return 0;
}

int XhciFtdiFinishClaim(UINT32 RootPort, UINT8 Speed) {
    UINT8 EpIn = 0;
    UINT8 EpOut = 0;
    UINT16 MpsIn = 64;
    UINT16 MpsOut = 64;
    UINT16 Total;
    UINT8 ConfigVal = 1;
    UINT16 Vid;
    UINT16 Pid;

    if (gFtdiSlot == 0) {
        return 0;
    }
    gXferSlot = gFtdiSlot;
    if (GetDeviceDesc() < 0) {
        goto fail;
    }
    Vid = (UINT16)(gCtrlBuf[8] | (gCtrlBuf[9] << 8));
    Pid = (UINT16)(gCtrlBuf[10] | (gCtrlBuf[11] << 8));
    if (Vid != FTDI_VID || !IsFtdiPid(Pid)) {
        goto fail;
    }
    if (GetDesc(0x0200, 0, 9, gFtdiCfgBuf) < 0) {
        goto fail;
    }
    Total = (UINT16)(gFtdiCfgBuf[2] | (gFtdiCfgBuf[3] << 8));
    if (Total < 9) {
        Total = 9;
    }
    if (Total > sizeof(gFtdiCfgBuf)) {
        Total = (UINT16)sizeof(gFtdiCfgBuf);
    }
    ConfigVal = gFtdiCfgBuf[5] ? gFtdiCfgBuf[5] : 1;
    if (GetDesc(0x0200, 0, Total, gFtdiCfgBuf) < 0) {
        goto fail;
    }
    if (!ParseFtdiBulk(gFtdiCfgBuf, Total, &EpIn, &MpsIn, &EpOut, &MpsOut)) {
        goto fail;
    }
    if (SetConfig(ConfigVal) < 0) {
        goto fail;
    }
    if (!XhciFtdiConfigBulk(gFtdiSlot, RootPort, Speed, EpIn, MpsIn, EpOut, MpsOut)) {
        goto fail;
    }
    if (FtdiSetLine115200() < 0) {
        BootLog("Boot: USB-UART Baud Fail\n");
        goto fail;
    }
    gFtdiPort = RootPort;
    gFtdiClaimed = 1;
    XhciFtdiRxArm();
    /* QEMU 默认 BootLog 会滤非 milestone；COM1+棒上都要看得见 */
    ToyLogBoot("Boot: USB-UART FTDI\n");
    ToyBootMarkUsb("Boot: USB-UART FTDI\n");
    return 1;

fail:
    if (gFtdiSlot != 0) {
        DisableSlot(gFtdiSlot);
        gFtdiSlot = 0;
    }
    return 0;
}

static int FtdiAddressRoot(UINT32 P, UINT8 *Speed) {
    UINT32 Ps;
    int Force = 0;

    if (!MscClaimForceUntilPed(P, &Force)) {
        return 0;
    }
    Ps = ReadMmio32(gOperationalBase + PortReg(P));
    *Speed = PortSpeed(Ps);
    if (gFtdiSlot != 0) {
        DisableSlot(gFtdiSlot);
        gFtdiSlot = 0;
    }
    gFtdiRoute = 0;
    gFtdiHubSlot = 0;
    gFtdiTtPort = 0;
    if (!AddressDeviceOnPort(P, *Speed, &gFtdiSlot, gFtdiDevCtx, 0, 0, 0, 0, 0)) {
        if (gFtdiSlot != 0) {
            DisableSlot(gFtdiSlot);
            gFtdiSlot = 0;
        }
        return 0;
    }
    if (gFtdiSlot <= DCBAA_SLOTS) {
        gSlotEp0UsesKbdRing[gFtdiSlot] = 0;
    }
    return 1;
}

int XhciFtdiTryHubChildren(void); /* XhciFtdiHub.c */

int XhciFtdiClaim(void) {
    UINT32 P;
    UINT8 Speed;

    if (!gXhciStarted || gOperationalBase == 0 || gMaxPorts == 0) {
        return -1;
    }
    if (gFtdiClaimed && gFtdiSlot != 0) {
        return 1;
    }
    if (gHubSlotId != 0 && XhciFtdiTryHubChildren()) {
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
        if (!FtdiAddressRoot(P, &Speed)) {
            continue;
        }
        if (XhciFtdiFinishClaim(P, Speed)) {
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
