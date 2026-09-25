/*
 * EhciMscHub.c — MSC 用 hub 控制（PR-H-ehci-3）
 */
#include "EhciPrivate.h"
#include "ToySerialLog.h"
#include "Hal.h"
#include "HalSerial.h"

#define HUB_FEAT_PORT_RESET        4u
#define HUB_FEAT_PORT_POWER        8u
#define HUB_FEAT_C_PORT_CONNECTION 16u
#define HUB_FEAT_C_PORT_RESET      20u
#define HUB_FEAT_C_PORT_ENABLE     17u
#define HUB_STAT_CONNECT           (1u << 0)
#define HUB_STAT_ENABLE            (1u << 1)
#define HUB_STAT_LOW_SPEED         (1u << 9)
#define HUB_STAT_HIGH_SPEED        (1u << 10)

void EhciMscZero(UINT8 *P, UINT32 N) {
    UINT32 i;
    for (i = 0; i < N; i++) {
        P[i] = 0;
    }
}

void EhciMscCopy(UINT8 *D, const UINT8 *S, UINT32 N) {
    UINT32 i;
    for (i = 0; i < N; i++) {
        D[i] = S[i];
    }
}

void EhciMscMarkHex4(const char *Prefix, UINT32 V) {
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

int EhciMscHubGetStatus(EHCI_CTRL *C, UINT8 HubAddr, UINT8 Port, UINT16 *Status,
                        UINT16 *Change) {
    USB_SETUP_PACKET S;
    UINT8 Buf[4];

    S.bmRequestType = 0xA3;
    S.bRequest = 0x00;
    S.wValue = 0;
    S.wIndex = Port;
    S.wLength = 4;
    C->XferSpeed = EHCI_SPEED_HS;
    C->XferHubAddr = 0;
    C->XferHubPort = 0;
    if (EhciControlXfer(C, HubAddr, 64, &S, Buf) != 0) {
        return 0;
    }
    *Status = (UINT16)(Buf[0] | ((UINT16)Buf[1] << 8));
    *Change = (UINT16)(Buf[2] | ((UINT16)Buf[3] << 8));
    return 1;
}

int EhciMscHubSetFeat(EHCI_CTRL *C, UINT8 HubAddr, UINT8 Port, UINT16 Feat) {
    USB_SETUP_PACKET S;

    S.bmRequestType = 0x23;
    S.bRequest = 0x03;
    S.wValue = Feat;
    S.wIndex = Port;
    S.wLength = 0;
    C->XferSpeed = EHCI_SPEED_HS;
    C->XferHubAddr = 0;
    C->XferHubPort = 0;
    return EhciControlXfer(C, HubAddr, 64, &S, 0) == 0;
}

int EhciMscHubClearFeat(EHCI_CTRL *C, UINT8 HubAddr, UINT8 Port, UINT16 Feat) {
    USB_SETUP_PACKET S;

    S.bmRequestType = 0x23;
    S.bRequest = 0x01;
    S.wValue = Feat;
    S.wIndex = Port;
    S.wLength = 0;
    C->XferSpeed = EHCI_SPEED_HS;
    C->XferHubAddr = 0;
    C->XferHubPort = 0;
    return EhciControlXfer(C, HubAddr, 64, &S, 0) == 0;
}

int EhciMscHubGetDesc(EHCI_CTRL *C, UINT8 HubAddr, UINT8 *Out, UINT16 Len) {
    USB_SETUP_PACKET S;

    S.bmRequestType = 0xA0;
    S.bRequest = 0x06;
    S.wValue = 0x2900;
    S.wIndex = 0;
    S.wLength = Len;
    C->XferSpeed = EHCI_SPEED_HS;
    C->XferHubAddr = 0;
    C->XferHubPort = 0;
    return EhciControlXfer(C, HubAddr, 64, &S, Out) == 0;
}

static UINT8 PortSpeed(UINT16 St) {
    if (St & HUB_STAT_LOW_SPEED) {
        return EHCI_SPEED_LS;
    }
    if (St & HUB_STAT_HIGH_SPEED) {
        return EHCI_SPEED_HS;
    }
    return EHCI_SPEED_FS;
}

int EhciMscHubResetPort(EHCI_CTRL *C, UINT8 HubAddr, UINT8 Port, UINT8 *SpeedOut) {
    UINT16 St;
    UINT16 Ch;
    int Spin;

    if (!EhciMscHubSetFeat(C, HubAddr, Port, HUB_FEAT_PORT_RESET)) {
        return 0;
    }
    Spin = 1000000;
    while (Spin-- > 0) {
        if (!EhciMscHubGetStatus(C, HubAddr, Port, &St, &Ch)) {
            return 0;
        }
        if ((St & (1u << 4)) == 0) {
            break;
        }
        HalCpuRelax();
    }
    (void)EhciMscHubClearFeat(C, HubAddr, Port, HUB_FEAT_C_PORT_RESET);
    (void)EhciMscHubClearFeat(C, HubAddr, Port, HUB_FEAT_C_PORT_ENABLE);
    EhciDelay(500000);
    if (!EhciMscHubGetStatus(C, HubAddr, Port, &St, &Ch)) {
        return 0;
    }
    if ((St & HUB_STAT_ENABLE) == 0) {
        EhciDelay(500000);
        if (!EhciMscHubGetStatus(C, HubAddr, Port, &St, &Ch) ||
            (St & HUB_STAT_ENABLE) == 0) {
            gEhciLastErr = "msc hub !EN";
            return 0;
        }
    }
    *SpeedOut = PortSpeed(St);
    return 1;
}

static int TryHubPortMsc(EHCI_CTRL *C, UINT8 HubAddr, UINT8 P) {
    UINT16 St;
    UINT16 Ch;
    UINT8 Sp;

    if (C->HidOk && C->HubAddr == HubAddr && C->HubPort == P) {
        return 0; /* 鼠口留给 HID */
    }
    if (!EhciMscHubGetStatus(C, HubAddr, P, &St, &Ch)) {
        return 0;
    }
    if (Ch & HUB_STAT_CONNECT) {
        (void)EhciMscHubClearFeat(C, HubAddr, P, HUB_FEAT_C_PORT_CONNECTION);
    }
    if ((St & HUB_STAT_CONNECT) == 0) {
        return 0;
    }
    if (!EhciMscHubResetPort(C, HubAddr, P, &Sp)) {
        return 0;
    }
    {
        const char *SpName =
            Sp == EHCI_SPEED_HS ? "HS" : (Sp == EHCI_SPEED_LS ? "LS" : "FS");
        ToyBootMarkUsb("Boot: EHCI MSC hub spd=");
        ToyBootMarkUsb(SpName);
        ToyBootMarkUsb("\n");
    }
    if (!EhciMscFinishClaim(C, Sp, HubAddr, P)) {
        return 0;
    }
    /* 空读卡器等：claim 后 capacity 失败则卸掉，继续扫下一口 */
    if (EhciMscCapacity() == 0) {
        return 1;
    }
    ToyBootMarkUsb("Boot: EHCI MSC drop (no capacity)\n");
    (void)EhciMscRelease();
    return 0;
}

int EhciMscClaimViaHub(EHCI_CTRL *C) {
    UINT8 HubAddr = C->HubAddr;
    UINT8 Desc[16];
    UINT8 NPorts = 4;
    UINT8 P;
    UINT8 Pass;

    if (!HubAddr) {
        return 0;
    }
    /* 扫口会 Reset 子设备；作废先前 EHCI FTDI claim（ehci-4） */
    EhciFtdiInvalidate();
    if (EhciMscHubGetDesc(C, HubAddr, Desc, 9)) {
        NPorts = Desc[2] ? Desc[2] : 4;
        if (NPorts > 14) {
            NPorts = 14;
        }
    }
    for (P = 1; P <= NPorts; P++) {
        (void)EhciMscHubSetFeat(C, HubAddr, P, HUB_FEAT_PORT_POWER);
    }
    EhciDelay(800000);

    for (Pass = 0; Pass < 2; Pass++) {
        if (Pass == 1) {
            EhciDelay(800000);
        }
        for (P = 1; P <= NPorts; P++) {
            if (TryHubPortMsc(C, HubAddr, P)) {
                return 1;
            }
        }
    }
    return 0;
}

