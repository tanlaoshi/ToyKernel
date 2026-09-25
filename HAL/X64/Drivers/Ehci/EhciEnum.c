/*
 * EhciEnum.c — 根口 / 设备枚举 boot HID（PR-H-ehci-2 · 2j）
 */
#include "EhciPrivate.h"
#include "ToySerialLog.h"
#include "Hal.h"
#include "HalSerial.h"

int EhciEnumGetDesc(EHCI_CTRL *C, UINT8 Addr, UINT8 EpMax, UINT16 TypeIndex,
                   UINT16 Len, void *Out) {
    USB_SETUP_PACKET S;

    S.bmRequestType = 0x80;
    S.bRequest = 0x06;
    S.wValue = TypeIndex;
    S.wIndex = 0;
    S.wLength = Len;
    return EhciControlXfer(C, Addr, EpMax, &S, Out);
}

int EhciEnumSetAddr(EHCI_CTRL *C, UINT8 NewAddr, UINT8 EpMax) {
    USB_SETUP_PACKET S;

    S.bmRequestType = 0x00;
    S.bRequest = 0x05;
    S.wValue = NewAddr;
    S.wIndex = 0;
    S.wLength = 0;
    return EhciControlXfer(C, 0, EpMax, &S, 0);
}

int EhciEnumSetConfig(EHCI_CTRL *C, UINT8 Addr, UINT8 EpMax, UINT8 Cfg) {
    USB_SETUP_PACKET S;

    S.bmRequestType = 0x00;
    S.bRequest = 0x09;
    S.wValue = Cfg;
    S.wIndex = 0;
    S.wLength = 0;
    return EhciControlXfer(C, Addr, EpMax, &S, 0);
}

int EhciEnumSetProtocolBoot(EHCI_CTRL *C, UINT8 Addr, UINT8 EpMax, UINT8 Iface) {
    USB_SETUP_PACKET S;

    S.bmRequestType = 0x21;
    S.bRequest = 0x0B;
    S.wValue = 0;
    S.wIndex = Iface;
    S.wLength = 0;
    return EhciControlXfer(C, Addr, EpMax, &S, 0);
}

int EhciEnumSetIdle(EHCI_CTRL *C, UINT8 Addr, UINT8 EpMax, UINT8 Iface) {
    USB_SETUP_PACKET S;

    S.bmRequestType = 0x21;
    S.bRequest = 0x0A;
    S.wValue = 0;
    S.wIndex = Iface;
    S.wLength = 0;
    return EhciControlXfer(C, Addr, EpMax, &S, 0);
}

void EhciEnumMarkHex4(const char *Prefix, UINT32 V) {
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

static int EnumRootPort(EHCI_CTRL *C, UINT8 Port) {
    USB_DEVICE_DESCRIPTOR Dev;
    UINT8 EpMax = 64;

    if (!EhciPortReset(C, Port)) {
        return 0;
    }

    C->XferSpeed = EHCI_SPEED_HS;
    C->XferHubAddr = 0;
    C->XferHubPort = 0;

    if (EhciEnumGetDesc(C, 0, 64, 0x0100, 8, &Dev) != 0) {
        if (EhciEnumGetDesc(C, 0, 8, 0x0100, 8, &Dev) != 0) {
            ToyBootMarkUsb("Boot: EHCI getdesc fail\n");
            return 0;
        }
        EpMax = 8;
    } else {
        EpMax = Dev.bMaxPacketSize0 ? Dev.bMaxPacketSize0 : 64;
    }
    if (EhciEnumGetDesc(C, 0, EpMax, 0x0100, 18, &Dev) != 0) {
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
        EhciEnumMarkHex4(" vid=", Dev.idVendor);
        EhciEnumMarkHex4(" pid=", Dev.idProduct);
        EhciEnumMarkHex4(" cls=", Dev.bDeviceClass);
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
        if (EhciEnumSetAddr(C, HubAddr, HubEp) != 0) {
            return 0;
        }
        C->XferSpeed = EHCI_SPEED_HS;
        C->XferHubAddr = 0;
        C->XferHubPort = 0;
        EhciDelay(100000);
        if (EhciEnumSetConfig(C, HubAddr, HubEp, 1) != 0) {
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
