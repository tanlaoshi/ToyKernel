/*
 * EhciEnum.c — 根口 / 设备枚举 boot HID（PR-H-ehci-2 · 2j）
 */
#include "EhciPrivate.h"
#include "ToySerialLog.h"
#include "Hal.h"
#include "HalSerial.h"

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

static int SetProtocolBoot(EHCI_CTRL *C, UINT8 Addr, UINT8 EpMax, UINT8 Iface) {
    USB_SETUP_PACKET S;

    S.bmRequestType = 0x21;
    S.bRequest = 0x0B;
    S.wValue = 0;
    S.wIndex = Iface;
    S.wLength = 0;
    return EhciControlXfer(C, Addr, EpMax, &S, 0);
}

static int SetIdle(EHCI_CTRL *C, UINT8 Addr, UINT8 EpMax, UINT8 Iface) {
    USB_SETUP_PACKET S;

    S.bmRequestType = 0x21;
    S.bRequest = 0x0A;
    S.wValue = 0;
    S.wIndex = Iface;
    S.wLength = 0;
    return EhciControlXfer(C, Addr, EpMax, &S, 0);
}

static void MarkHex4(const char *Prefix, UINT32 V) {
    char Hex[12];
    char Dig[5];

    ToyBootMarkUsb(Prefix);
    HalSerialFormatHex(Hex, V, 4);
    Dig[0] = Hex[2];
    Dig[1] = Hex[3];
    Dig[2] = Hex[4];
    Dig[3] = Hex[5];
    Dig[4] = 0;
    ToyBootMarkUsb(Dig);
}

static int ParseHidIface(UINT8 *Cfg, UINT16 Len, UINT8 WantProto, UINT8 *Iface,
                         UINT8 *Proto, UINT8 *Ep, UINT16 *MaxPkt, UINT8 *Interval,
                         UINT8 *CfgVal) {
    UINT16 Off = 0;
    UINT8 CurIface = 0;
    UINT8 CurProto = 0;
    UINT8 CurSub = 0;
    int InHid = 0;
    int Found = 0;
    UINT8 BestScore = 0;

    *Iface = 0;
    *Proto = 0;
    *Ep = 0;
    *MaxPkt = 8;
    *Interval = 10;
    *CfgVal = 1;

    while (Off + 1 < Len) {
        UINT8 L = Cfg[Off];
        UINT8 T = Cfg[Off + 1];
        if (L < 2 || Off + L > Len) {
            break;
        }
        if (T == 0x02 && L >= 9) {
            *CfgVal = Cfg[Off + 5];
        } else if (T == 0x04 && L >= 9) {
            CurIface = Cfg[Off + 2];
            CurSub = Cfg[Off + 6];
            CurProto = Cfg[Off + 7];
            InHid = (Cfg[Off + 5] == 0x03u);
            if (WantProto == 1 && CurProto != 0 && CurProto != 1) {
                InHid = 0;
            }
            if (WantProto == 2 && CurProto != 0 && CurProto != 2) {
                InHid = 0;
            }
            (void)CurSub;
        } else if (T == 0x05 && L >= 7 && InHid) {
            UINT8 Addr = Cfg[Off + 2];
            UINT8 Attr = Cfg[Off + 3];
            if ((Addr & 0x80u) && ((Attr & 0x03u) == 0x03u)) {
                UINT8 Score = 1;
                if (CurSub == 0x01u) {
                    Score += 2;
                }
                if (WantProto && CurProto == WantProto) {
                    Score += 2;
                }
                if (WantProto == 0 && (CurProto == 1 || CurProto == 2)) {
                    Score += 1;
                }
                if (Score >= BestScore) {
                    BestScore = Score;
                    *Iface = CurIface;
                    *Proto = CurProto ? CurProto : (WantProto ? WantProto : 2);
                    *Ep = (UINT8)(Addr & 0x0Fu);
                    *MaxPkt =
                        (UINT16)(Cfg[Off + 4] | ((UINT16)Cfg[Off + 5] << 8));
                    *Interval = Cfg[Off + 6];
                    Found = 1;
                    if (Score >= 4) {
                        return 1;
                    }
                }
            }
        }
        Off = (UINT16)(Off + L);
    }
    return Found;
}

/* 在当前 XferSpeed/Hub* 下枚举 addr0 设备为 HID */
int EhciEnumDevice(EHCI_CTRL *C, UINT8 Speed, UINT8 HubAddr, UINT8 HubPort) {
    USB_DEVICE_DESCRIPTOR Dev;
    UINT8 Cfg[256];
    UINT8 EpMax = 64;
    UINT8 Addr;
    UINT8 Iface;
    UINT8 Proto;
    UINT8 Ep;
    UINT16 MaxPkt;
    UINT8 Interval;
    UINT8 CfgVal;
    UINT16 CfgLen;

    C->XferSpeed = Speed;
    C->XferHubAddr = HubAddr;
    C->XferHubPort = HubPort;

    if (GetDesc(C, 0, (Speed == EHCI_SPEED_HS) ? 64 : 8, 0x0100, 8, &Dev) !=
        0) {
        if (GetDesc(C, 0, 8, 0x0100, 8, &Dev) != 0) {
            ToyBootMarkUsb("Boot: EHCI getdesc fail ");
            ToyBootMarkUsb(gEhciLastErr ? gEhciLastErr : "?");
            ToyBootMarkUsb("\n");
            return 0;
        }
        EpMax = 8;
    } else {
        EpMax = Dev.bMaxPacketSize0 ? Dev.bMaxPacketSize0 : 8;
        if (Speed == EHCI_SPEED_HS && EpMax < 64) {
            /* keep */
        }
    }
    if (GetDesc(C, 0, EpMax, 0x0100, 18, &Dev) != 0) {
        gEhciLastErr = "devdesc";
        return 0;
    }

    MarkHex4("Boot: EHCI dev vid=", Dev.idVendor);
    MarkHex4(" pid=", Dev.idProduct);
    MarkHex4(" cls=", Dev.bDeviceClass);
    ToyBootMarkUsb("\n");

    if (Dev.bDeviceClass == 0x09u) {
        gEhciLastErr = "nested hub";
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
    if (!ParseHidIface(Cfg, CfgLen, 2, &Iface, &Proto, &Ep, &MaxPkt, &Interval,
                       &CfgVal) &&
        !ParseHidIface(Cfg, CfgLen, 1, &Iface, &Proto, &Ep, &MaxPkt, &Interval,
                       &CfgVal) &&
        !ParseHidIface(Cfg, CfgLen, 0, &Iface, &Proto, &Ep, &MaxPkt, &Interval,
                       &CfgVal)) {
        gEhciLastErr = "no hid iface";
        ToyBootMarkUsb("Boot: EHCI no hid iface\n");
        return 0;
    }
    if (SetConfig(C, Addr, EpMax, CfgVal) != 0) {
        return 0;
    }
    (void)SetProtocolBoot(C, Addr, EpMax, Iface);
    (void)SetIdle(C, Addr, EpMax, Iface);

    C->DevAddr = Addr;
    C->DevSpeed = Speed;
    C->HubAddr = HubAddr;
    C->HubPort = HubPort;
    C->HidIface = Iface;
    C->HidEp = Ep;
    C->HidProto = Proto ? Proto : 2;
    C->HidMaxPkt = MaxPkt ? MaxPkt : 8;
    C->HidInterval = Interval ? Interval : 10;
    C->HidOk = 1;
    gEhciLastErr = "ok";
    ToyBootMarkUsb(C->HidProto == 1 ? "Boot: EHCI HID kbd\n"
                                    : "Boot: EHCI HID mouse\n");
    return 1;
}

static int EnumRootPort(EHCI_CTRL *C, UINT8 Port) {
    USB_DEVICE_DESCRIPTOR Dev;
    UINT8 EpMax = 64;

    if (!EhciPortReset(C, Port)) {
        return 0;
    }

    C->XferSpeed = EHCI_SPEED_HS;
    C->XferHubAddr = 0;
    C->XferHubPort = 0;

    if (GetDesc(C, 0, 64, 0x0100, 8, &Dev) != 0) {
        if (GetDesc(C, 0, 8, 0x0100, 8, &Dev) != 0) {
            ToyBootMarkUsb("Boot: EHCI getdesc fail\n");
            return 0;
        }
        EpMax = 8;
    } else {
        EpMax = Dev.bMaxPacketSize0 ? Dev.bMaxPacketSize0 : 64;
    }
    if (GetDesc(C, 0, EpMax, 0x0100, 18, &Dev) != 0) {
        gEhciLastErr = "devdesc";
        return 0;
    }

    {
        char Line[4];
        Line[0] = 'p';
        Line[1] = (char)('0' + (Port % 10));
        Line[2] = 0;
        ToyBootMarkUsb("Boot: EHCI ");
        ToyBootMarkUsb(Line);
        MarkHex4(" vid=", Dev.idVendor);
        MarkHex4(" pid=", Dev.idProduct);
        MarkHex4(" cls=", Dev.bDeviceClass);
        ToyBootMarkUsb("\n");
    }

    if (Dev.bDeviceClass == 0x09u) {
        /* 根口是 Intel RMH 等：配置 hub 后扫下游 */
        UINT8 HubAddr;
        UINT8 HubEp = EpMax;

        if (gEhciNextAddr < 2 || gEhciNextAddr > 127) {
            gEhciNextAddr = 2;
        }
        HubAddr = gEhciNextAddr++;
        if (SetAddr(C, HubAddr, HubEp) != 0) {
            return 0;
        }
        C->XferSpeed = EHCI_SPEED_HS;
        C->XferHubAddr = 0;
        C->XferHubPort = 0;
        EhciDelay(100000);
        if (SetConfig(C, HubAddr, HubEp, 1) != 0) {
            gEhciLastErr = "hub cfg";
            return 0;
        }
        C->HubAddr = HubAddr;
        ToyBootMarkUsb("Boot: EHCI hub walk\n");
        return EhciEnumHub(C);
    }

    /* 根口直接 HID（少见） */
    C->XferSpeed = EHCI_SPEED_HS;
    C->XferHubAddr = 0;
    C->XferHubPort = 0;
    /* 设备已在 addr0 读过 desc；再走完整 EnumDevice 会重复 getdesc——直接用 EnumDevice */
    return EhciEnumDevice(C, EHCI_SPEED_HS, 0, 0);
}

int EhciEnumHid(EHCI_CTRL *C) {
    UINT8 P;
    int AnyCcs = 0;
    const char *Saved = 0;

    if (!C || !C->Sched) {
        return 0;
    }
    EhciSurveyCcs(C);
    for (P = 1; P <= C->NPorts; P++) {
        if ((C->CcsMask & (1u << (P - 1))) == 0) {
            continue;
        }
        AnyCcs = 1;
        if (EnumRootPort(C, P)) {
            return 1;
        }
        /* 保留 tok= / hub 相关错，勿被下一根口盖掉 */
        if (gEhciLastErr && (gEhciLastErr[0] == 't' || gEhciLastErr[0] == 'h' ||
                             gEhciLastErr[0] == 'n')) {
            Saved = gEhciLastErr;
        }
    }
    if (!AnyCcs) {
        gEhciLastErr = "ccs=0";
        for (P = 1; P <= C->NPorts; P++) {
            if (EnumRootPort(C, P)) {
                return 1;
            }
        }
    } else if (Saved) {
        gEhciLastErr = Saved;
    }
    return 0;
}
