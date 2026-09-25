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

static int ParseFtdiBulk(UINT8 *Cfg, UINT16 Total, UINT8 *EpOut, UINT16 *MpsOut) {
    UINT16 Off = 0;
    UINT8 CurIface = 0xFF;
    UINT8 CurAlt = 0;
    UINT8 BestOut = 0;
    UINT16 BestMps = 0;

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

            if ((Attr & 0x03) == 2 && (Addr & 0x80) == 0) {
                BestOut = Addr;
                BestMps = Mps ? Mps : 64;
            }
        }
        Off = (UINT16)(Off + Len);
    }
    if (BestOut == 0) {
        return 0;
    }
    if (EpOut) {
        *EpOut = BestOut;
    }
    if (MpsOut) {
        *MpsOut = BestMps;
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

static int ConfigureFtdiBulkOut(UINT32 SlotId, UINT32 RootPort, UINT8 Speed,
                                UINT8 EpOut, UINT16 MpsOut) {
    UINT8 OutNum = EpOut & 0x0F;
    UINT32 OutDci = (UINT32)OutNum * 2 + 0;
    UINT32 *Slot;
    UINT32 *Ep;
    UINT64 Deq;

    if (MpsOut == 0 || MpsOut > 512) {
        MpsOut = 64;
    }
    ZeroMemory(gInCtx, sizeof(gInCtx));
    *(UINT32 *)(void *)(gInCtx + 4) = (1u << 0) | (1u << OutDci);

    Slot = (UINT32 *)(void *)InSlot();
    Slot[0] = (OutDci << 27) | ((UINT32)Speed << 20) | (gFtdiRoute & 0xFFFFFu);
    Slot[1] = (UINT32)RootPort << 16;
    if (gFtdiHubSlot != 0 && Speed < 3) {
        Slot[2] = (UINT32)gFtdiHubSlot | ((UINT32)gFtdiTtPort << 8);
    }

    InitRing(gFtdiBulkOutRing, &gFtdiBulkOut, RING_SIZE);
    Ep = (UINT32 *)(void *)InEp(OutDci);
    Ep[0] = 0;
    Ep[1] = (3u << 1) | (2u << 3) | ((UINT32)MpsOut << 16);
    Deq = PointerToPhysical(gFtdiBulkOutRing) | 1;
    Ep[2] = (UINT32)Deq;
    Ep[3] = (UINT32)(Deq >> 32);
    Ep[4] = (UINT32)MpsOut;

    FlushDma(gInCtx, sizeof(gInCtx));
    FlushDma(gFtdiBulkOutRing, sizeof(gFtdiBulkOutRing));
    FlushDma(gFtdiDevCtx, sizeof(gFtdiDevCtx));
    if (Command(PointerToPhysical(gInCtx), TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(SlotId), 0) <
        0) {
        BootLog("Boot: usb-uart cfg ep fail\n");
        return 0;
    }
    gFtdiBulkOutDci = OutDci;
    gFtdiBulkOutMps = MpsOut;
    return 1;
}

int XhciFtdiFinishClaim(UINT32 RootPort, UINT8 Speed) {
    UINT8 EpOut = 0;
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
    if (!ParseFtdiBulk(gFtdiCfgBuf, Total, &EpOut, &MpsOut)) {
        goto fail;
    }
    if (SetConfig(ConfigVal) < 0) {
        goto fail;
    }
    if (!ConfigureFtdiBulkOut(gFtdiSlot, RootPort, Speed, EpOut, MpsOut)) {
        goto fail;
    }
    if (FtdiSetLine115200() < 0) {
        BootLog("Boot: usb-uart baud fail\n");
        goto fail;
    }
    gFtdiPort = RootPort;
    gFtdiClaimed = 1;
    BootLog("boot: usb-uart ftdi\n");
    BootLogHex("Boot: usb-uart pid=", Pid, 4);
    BootLogHex("Boot: usb-uart port=", RootPort, 2);
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
