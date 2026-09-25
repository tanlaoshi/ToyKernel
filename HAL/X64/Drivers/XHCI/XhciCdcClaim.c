/*
 * XhciCdcClaim.c — PR-H-usb-uart-cdc-1：CDC-ACM 认领（Comm 0x02/02/01 + Data 0x0A）
 */
#include "XHCI/XhciInternal.h"

UINT32 gCdcSlot;
UINT32 gCdcPort;
UINT32 gCdcRoute;
UINT8 gCdcHubSlot;
UINT8 gCdcTtPort;
UINT8 gCdcCommIface;
UINT32 gCdcBulkOutDci;
UINT16 gCdcBulkOutMps;
UINT32 gCdcBulkInDci;
UINT16 gCdcBulkInMps;
int gCdcClaimed;

UINT8 gCdcDevCtx[2048] __attribute__((aligned(64)));
XHCI_TRB gCdcEp0Ring[RING_SIZE] __attribute__((aligned(64)));
RING_STATE gCdcEp0;
XHCI_TRB gCdcBulkOutRing[RING_SIZE] __attribute__((aligned(64)));
RING_STATE gCdcBulkOut;
XHCI_TRB gCdcBulkInRing[RING_SIZE] __attribute__((aligned(64)));
RING_STATE gCdcBulkIn;
static UINT8 gCdcCfgBuf[512] __attribute__((aligned(64)));
static UINT8 gCdcLineCoding[7] __attribute__((aligned(64)));

void XhciCdcEp0Ring(XHCI_TRB **RingOut, RING_STATE **StOut) {
    *RingOut = gCdcEp0Ring;
    *StOut = &gCdcEp0;
}

int XhciCdcReady(void) {
    return (gCdcClaimed && gCdcSlot != 0) ? 1 : 0;
}

/* Comm iface + Data Bulk IN/OUT；不对则 0 */
static int ParseCdcAcm(UINT8 *Cfg, UINT16 Total, UINT8 *CommIface,
                       UINT8 *EpIn, UINT16 *MpsIn, UINT8 *EpOut, UINT16 *MpsOut) {
    UINT16 Off = 0;
    UINT8 CurIface = 0xFF;
    UINT8 CurAlt = 0;
    UINT8 CurClass = 0;
    UINT8 CurSub = 0;
    UINT8 CurProto = 0;
    UINT8 BestComm = 0xFF;
    UINT8 DataIface = 0xFF;
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
            CurClass = Cfg[Off + 5];
            CurSub = Cfg[Off + 6];
            CurProto = Cfg[Off + 7];
            if (CurClass == 0x02 && CurSub == 0x02 && CurProto == 0x01 &&
                BestComm == 0xFF) {
                BestComm = CurIface;
            }
            if (CurClass == 0x0A && DataIface == 0xFF) {
                DataIface = CurIface;
            }
        } else if (Type == 5 && Len >= 7 && CurIface != 0xFF && CurAlt == 0 &&
                   CurIface == DataIface && CurClass == 0x0A) {
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
    if (BestComm == 0xFF || BestIn == 0 || BestOut == 0) {
        return 0;
    }
    if (CommIface) {
        *CommIface = BestComm;
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

static int CdcSetLine115200(UINT8 CommIface) {
    USB_SETUP_PACKET Setup;

    /* dwDTERate=115200 LE, 1 stop, no parity, 8 bits */
    gCdcLineCoding[0] = 0x00;
    gCdcLineCoding[1] = 0xC2;
    gCdcLineCoding[2] = 0x01;
    gCdcLineCoding[3] = 0x00;
    gCdcLineCoding[4] = 0;
    gCdcLineCoding[5] = 0;
    gCdcLineCoding[6] = 8;
    Setup.bmRequestType = 0x21;
    Setup.bRequest = 0x20; /* SET_LINE_CODING */
    Setup.wValue = 0;
    Setup.wIndex = CommIface;
    Setup.wLength = 7;
    if (ControlXfer(&Setup, gCdcLineCoding) < 0) {
        return -1;
    }
    Setup.bRequest = 0x22; /* SET_CONTROL_LINE_STATE */
    Setup.wValue = 0x0003; /* DTR|RTS */
    Setup.wLength = 0;
    (void)ControlXfer(&Setup, 0);
    return 0;
}

int XhciCdcConfigBulk(UINT32 SlotId, UINT32 RootPort, UINT8 Speed,
                      UINT8 EpIn, UINT16 MpsIn, UINT8 EpOut, UINT16 MpsOut);

int XhciCdcFinishClaim(UINT32 RootPort, UINT8 Speed) {
    UINT8 EpIn = 0;
    UINT8 EpOut = 0;
    UINT8 Comm = 0;
    UINT16 MpsIn = 64;
    UINT16 MpsOut = 64;
    UINT16 Total;
    UINT8 ConfigVal = 1;

    if (gCdcSlot == 0) {
        return 0;
    }
    gXferSlot = gCdcSlot;
    if (GetDeviceDesc() < 0) {
        goto fail;
    }
    if (GetDesc(0x0200, 0, 9, gCdcCfgBuf) < 0) {
        goto fail;
    }
    Total = (UINT16)(gCdcCfgBuf[2] | (gCdcCfgBuf[3] << 8));
    if (Total < 9) {
        Total = 9;
    }
    if (Total > sizeof(gCdcCfgBuf)) {
        Total = (UINT16)sizeof(gCdcCfgBuf);
    }
    ConfigVal = gCdcCfgBuf[5] ? gCdcCfgBuf[5] : 1;
    if (GetDesc(0x0200, 0, Total, gCdcCfgBuf) < 0) {
        goto fail;
    }
    if (!ParseCdcAcm(gCdcCfgBuf, Total, &Comm, &EpIn, &MpsIn, &EpOut, &MpsOut)) {
        goto fail;
    }
    if (SetConfig(ConfigVal) < 0) {
        goto fail;
    }
    gCdcCommIface = Comm;
    if (!XhciCdcConfigBulk(gCdcSlot, RootPort, Speed, EpIn, MpsIn, EpOut, MpsOut)) {
        goto fail;
    }
    if (CdcSetLine115200(Comm) < 0) {
        BootLog("Boot: usb-uart cdc line fail\n");
        goto fail;
    }
    /* QEMU usb-serial：Config/Line 后需短暂就绪，否则首包 Bulk OUT 易超时 */
    if (HalCpuIsHypervisor()) {
        StallMs(20);
    } else {
        StallMs(50);
    }
    gCdcPort = RootPort;
    gCdcClaimed = 1;
    /* COM1 + CDC 双路；BootMark 保证 QEMU 非 verbose 也看得见 */
    ToyLogBoot("boot: usb-uart cdc\n");
    ToyBootMarkUsb("boot: usb-uart cdc\n");
    return 1;

fail:
    if (gCdcSlot != 0) {
        DisableSlot(gCdcSlot);
        gCdcSlot = 0;
    }
    return 0;
}

static int CdcAddressRoot(UINT32 P, UINT8 *Speed) {
    UINT32 Ps;
    int Force = 0;

    if (!MscClaimForceUntilPed(P, &Force)) {
        return 0;
    }
    Ps = ReadMmio32(gOperationalBase + PortReg(P));
    *Speed = PortSpeed(Ps);
    if (gCdcSlot != 0) {
        DisableSlot(gCdcSlot);
        gCdcSlot = 0;
    }
    gCdcRoute = 0;
    gCdcHubSlot = 0;
    gCdcTtPort = 0;
    if (!AddressDeviceOnPort(P, *Speed, &gCdcSlot, gCdcDevCtx, 0, 0, 0, 0, 0)) {
        if (gCdcSlot != 0) {
            DisableSlot(gCdcSlot);
            gCdcSlot = 0;
        }
        return 0;
    }
    if (gCdcSlot <= DCBAA_SLOTS) {
        gSlotEp0UsesKbdRing[gCdcSlot] = 0;
    }
    return 1;
}

/*
 * 已有 FTDI 则跳过（互斥）。返回 1 认领；0 无棒；-1 无 HC。
 */
int XhciCdcClaim(void) {
    UINT32 P;
    UINT8 Speed;

    if (!gXhciStarted || gOperationalBase == 0 || gMaxPorts == 0) {
        return -1;
    }
    if (gFtdiClaimed) {
        return 0;
    }
    if (gCdcClaimed && gCdcSlot != 0) {
        return 1;
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
        if (!CdcAddressRoot(P, &Speed)) {
            continue;
        }
        if (XhciCdcFinishClaim(P, Speed)) {
            if (gSlotId != 0 && gIntrDci != 0) {
                QueueIntr();
            }
            if (gMouseSlotId != 0 && gMouseIntrDci != 0) {
                QueueMouseIntr();
            }
            return 1;
        }
    }
    return 0;
}
