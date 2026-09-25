/*
 * EhciMscClaim.c — Parse Bulk + FinishClaim / hub 子口（PR-H-ehci-3）
 */
#include "EhciPrivate.h"
#include "ToySerialLog.h"
#include "Hal.h"

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

static int ParseMscBulk(UINT8 *Cfg, UINT16 Total, UINT8 *OutIface, UINT8 *EpIn,
                        UINT16 *MpsIn, UINT8 *EpOut, UINT16 *MpsOut,
                        UINT8 *CfgVal) {
    UINT16 Off = 0;
    UINT8 CurIface = 0xFF;
    UINT8 CurAlt = 0;
    UINT8 IfaceClass = 0;
    UINT8 IfaceSub = 0;
    UINT8 IfaceProto = 0;
    UINT8 BestIface = 0xFF;
    UINT8 BestIn = 0;
    UINT8 BestOut = 0;
    UINT16 BestInMps = 0;
    UINT16 BestOutMps = 0;
    int BestScore = -1;

    *CfgVal = 1;
    while (Off + 2 <= Total) {
        UINT8 Len = Cfg[Off];
        UINT8 Type = Cfg[Off + 1];

        if (Len < 2 || Off + Len > Total) {
            break;
        }
        if (Type == 0x02 && Len >= 9) {
            *CfgVal = Cfg[Off + 5];
        } else if (Type == 4 && Len >= 9) {
            CurIface = Cfg[Off + 2];
            CurAlt = Cfg[Off + 3];
            IfaceClass = Cfg[Off + 5];
            IfaceSub = Cfg[Off + 6];
            IfaceProto = Cfg[Off + 7];
        } else if (Type == 5 && Len >= 7 && CurIface != 0xFF && CurAlt == 0) {
            UINT8 Addr = Cfg[Off + 2];
            UINT8 Attr = Cfg[Off + 3];
            UINT16 Mps = (UINT16)(Cfg[Off + 4] | (Cfg[Off + 5] << 8));
            int Score;

            if ((Attr & 0x03) != 2) {
                Off = (UINT16)(Off + Len);
                continue;
            }
            if (IfaceClass == 0x08) {
                Score = 10;
                if (IfaceSub == 0x06) {
                    Score += 2;
                }
                if (IfaceProto == 0x50 || IfaceProto == 0x62) {
                    Score += 4;
                }
            } else {
                Score = 0;
            }
            if (Score < BestScore) {
                Off = (UINT16)(Off + Len);
                continue;
            }
            if (Score > BestScore) {
                BestScore = Score;
                BestIface = CurIface;
                BestIn = 0;
                BestOut = 0;
                BestInMps = 0;
                BestOutMps = 0;
            } else if (CurIface != BestIface) {
                Off = (UINT16)(Off + Len);
                continue;
            }
            if (Addr & 0x80) {
                BestIn = Addr;
                BestInMps = Mps;
            } else {
                BestOut = Addr;
                BestOutMps = Mps;
            }
        }
        Off = (UINT16)(Off + Len);
    }
    if (BestIn == 0 || BestOut == 0 || BestScore < 10) {
        return 0; /* 须 class8；勿误认 HID/BT */
    }
    *OutIface = BestIface;
    *EpIn = BestIn;
    *MpsIn = BestInMps;
    *EpOut = BestOut;
    *MpsOut = BestOutMps;
    return 1;
}

int EhciMscFinishClaim(EHCI_CTRL *C, UINT8 Speed, UINT8 HubAddr, UINT8 HubPort) {
    USB_DEVICE_DESCRIPTOR Dev;
    UINT8 Cfg[256];
    UINT8 EpMax = 64;
    UINT8 Addr;
    UINT8 Iface;
    UINT8 EpIn;
    UINT8 EpOut;
    UINT16 MpsIn;
    UINT16 MpsOut;
    UINT8 CfgVal;
    UINT16 CfgLen;

    C->XferSpeed = Speed;
    C->XferHubAddr = HubAddr;
    C->XferHubPort = HubPort;

    if (GetDesc(C, 0, (Speed == EHCI_SPEED_HS) ? 64 : 8, 0x0100, 8, &Dev) !=
        0) {
        if (GetDesc(C, 0, 8, 0x0100, 8, &Dev) != 0) {
            ToyBootMarkUsb("Boot: EHCI MSC getdesc fail\n");
            return 0;
        }
        EpMax = 8;
    } else {
        EpMax = Dev.bMaxPacketSize0 ? Dev.bMaxPacketSize0 : 8;
    }
    if (GetDesc(C, 0, EpMax, 0x0100, 18, &Dev) != 0) {
        return 0;
    }
    EhciMscMarkHex4("Boot: EHCI MSC vid=", Dev.idVendor);
    EhciMscMarkHex4(" pid=", Dev.idProduct);
    EhciMscMarkHex4(" cls=", Dev.bDeviceClass);
    ToyBootMarkUsb("\n");

    if (Dev.bDeviceClass == 0x09u) {
        gEhciLastErr = "msc nested hub";
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
    if (!ParseMscBulk(Cfg, CfgLen, &Iface, &EpIn, &MpsIn, &EpOut, &MpsOut,
                      &CfgVal)) {
        gEhciLastErr = "no msc iface";
        ToyBootMarkUsb("Boot: EHCI no msc iface\n");
        return 0;
    }
    if (SetConfig(C, Addr, EpMax, CfgVal) != 0) {
        return 0;
    }
    EhciDelay(200000);
    /* GET MAX LUN（忽略失败；部分棒不开此请求就不吐 BOT） */
    {
        USB_SETUP_PACKET S;
        UINT8 Lun = 0;
        S.bmRequestType = 0xA1;
        S.bRequest = 0xFE;
        S.wValue = 0;
        S.wIndex = Iface;
        S.wLength = 1;
        C->XferSpeed = Speed;
        C->XferHubAddr = HubAddr;
        C->XferHubPort = HubPort;
        (void)EhciControlXfer(C, Addr, EpMax, &S, &Lun);
    }

    C->MscAddr = Addr;
    C->MscSpeed = Speed;
    C->MscHubAddr = HubAddr;
    C->MscHubPort = HubPort;
    C->MscIface = Iface;
    C->MscEpIn = EpIn;
    C->MscEpOut = EpOut;
    /* HS Bulk 描述符 MPS=0 时必须 512，勿用 64 */
    if (MpsIn == 0) {
        MpsIn = (Speed == EHCI_SPEED_HS) ? 512 : 64;
    }
    if (MpsOut == 0) {
        MpsOut = (Speed == EHCI_SPEED_HS) ? 512 : 64;
    }
    C->MscMpsIn = MpsIn;
    C->MscMpsOut = MpsOut;
    C->MscDtIn = 0;
    C->MscDtOut = 0;
    C->MscEpMax0 = EpMax;
    C->MscOk = 1;
    C->MscCapacityOk = 0;
    C->MscBlockCount = 0;
    C->MscBlockSize = 0;
    gEhciMscCtrl = C;
    gEhciLastErr = "msc ok";
    ToyBootMarkUsb("Boot: EHCI MSC claimed\n");
    return 1;
}

int EhciMscEnsureHub(EHCI_CTRL *C, UINT8 Port) {
    USB_DEVICE_DESCRIPTOR Dev;
    UINT8 EpMax = 64;
    UINT8 HubAddr;

    if (!EhciPortReset(C, Port)) {
        return 0;
    }
    C->XferSpeed = EHCI_SPEED_HS;
    C->XferHubAddr = 0;
    C->XferHubPort = 0;
    if (GetDesc(C, 0, 64, 0x0100, 8, &Dev) != 0) {
        if (GetDesc(C, 0, 8, 0x0100, 8, &Dev) != 0) {
            return 0;
        }
        EpMax = 8;
    } else {
        EpMax = Dev.bMaxPacketSize0 ? Dev.bMaxPacketSize0 : 64;
    }
    if (GetDesc(C, 0, EpMax, 0x0100, 18, &Dev) != 0) {
        return 0;
    }
    if (Dev.bDeviceClass == 0x09u) {
        if (gEhciNextAddr < 2 || gEhciNextAddr > 127) {
            gEhciNextAddr = 2;
        }
        HubAddr = gEhciNextAddr++;
        if (SetAddr(C, HubAddr, EpMax) != 0) {
            return 0;
        }
        C->XferSpeed = EHCI_SPEED_HS;
        C->XferHubAddr = 0;
        C->XferHubPort = 0;
        EhciDelay(100000);
        if (SetConfig(C, HubAddr, EpMax, 1) != 0) {
            return 0;
        }
        C->HubAddr = HubAddr;
        ToyBootMarkUsb("Boot: EHCI MSC hub ready\n");
        return EhciMscClaimViaHub(C);
    }
    /* 根口直连 MSC */
    if (!EhciMscFinishClaim(C, EHCI_SPEED_HS, 0, 0)) {
        return 0;
    }
    if (EhciMscCapacity() == 0) {
        return 1;
    }
    ToyBootMarkUsb("Boot: EHCI MSC drop (no capacity)\n");
    (void)EhciMscRelease();
    return 0;
}

