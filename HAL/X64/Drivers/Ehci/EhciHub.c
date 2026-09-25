/*
 * EhciHub.c — 单层 HS hub（Intel RMH）下游枚举（PR-H-ehci-2 · 2l）
 *
 * 2k 后 hub+1 已是蓝牙 13D3:3362（split 通了）；鼠应在另一 RMH / 未亮 CCS 的口。
 * 本刀：每口打 st=；双遍扫描；加长上电。
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

char gEhciHubNote[96];

static int HubGetStatus(EHCI_CTRL *C, UINT8 HubAddr, UINT8 Port, UINT16 *Status,
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

static int HubSetFeat(EHCI_CTRL *C, UINT8 HubAddr, UINT8 Port, UINT16 Feat) {
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

static int HubClearFeat(EHCI_CTRL *C, UINT8 HubAddr, UINT8 Port, UINT16 Feat) {
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

static int HubGetDesc(EHCI_CTRL *C, UINT8 HubAddr, UINT8 *Out, UINT16 Len) {
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

static int HubResetPort(EHCI_CTRL *C, UINT8 HubAddr, UINT8 Port, UINT8 *SpeedOut) {
    UINT16 St;
    UINT16 Ch;
    int Spin;

    if (!HubSetFeat(C, HubAddr, Port, HUB_FEAT_PORT_RESET)) {
        return 0;
    }
    Spin = 1000000;
    while (Spin-- > 0) {
        if (!HubGetStatus(C, HubAddr, Port, &St, &Ch)) {
            return 0;
        }
        if ((St & (1u << 4)) == 0) {
            break;
        }
        HalCpuRelax();
    }
    (void)HubClearFeat(C, HubAddr, Port, HUB_FEAT_C_PORT_RESET);
    (void)HubClearFeat(C, HubAddr, Port, HUB_FEAT_C_PORT_ENABLE);
    EhciDelay(500000);
    if (!HubGetStatus(C, HubAddr, Port, &St, &Ch)) {
        return 0;
    }
    if ((St & HUB_STAT_ENABLE) == 0) {
        EhciDelay(500000);
        if (!HubGetStatus(C, HubAddr, Port, &St, &Ch) ||
            (St & HUB_STAT_ENABLE) == 0) {
            gEhciLastErr = "hub port !EN";
            return 0;
        }
    }
    *SpeedOut = PortSpeed(St);
    return 1;
}

static int TryHubPort(EHCI_CTRL *C, UINT8 HubAddr, UINT8 P) {
    UINT16 St;
    UINT16 Ch;
    UINT8 Sp;
    char Dig[3];

    if (!HubGetStatus(C, HubAddr, P, &St, &Ch)) {
        return 0;
    }
    Dig[0] = (char)('0' + (P % 10));
    Dig[1] = 0;
    ToyBootMarkUsb("Boot: EHCI hub+");
    ToyBootMarkUsb(Dig);
    MarkHex4(" st=", St);
    MarkHex4(" ch=", Ch);
    ToyBootMarkUsb("\n");

    if (Ch & HUB_STAT_CONNECT) {
        (void)HubClearFeat(C, HubAddr, P, HUB_FEAT_C_PORT_CONNECTION);
    }
    if ((St & HUB_STAT_CONNECT) == 0) {
        return 0;
    }
    if (!HubResetPort(C, HubAddr, P, &Sp)) {
        return 0;
    }
    {
        const char *SpName =
            Sp == EHCI_SPEED_HS ? "HS" : (Sp == EHCI_SPEED_LS ? "LS" : "FS");
        ToyBootMarkUsb("Boot: EHCI hub spd=");
        ToyBootMarkUsb(SpName);
        ToyBootMarkUsb("\n");
    }
    return EhciEnumDevice(C, Sp, HubAddr, P);
}

int EhciEnumHub(EHCI_CTRL *C) {
    UINT8 HubAddr = C->HubAddr;
    UINT8 Desc[16];
    UINT8 NPorts = 4;
    UINT8 P;
    UINT8 Pass;
    int NoteN = 0;
    UINT8 Seen[16];
    int i;

    if (!HubAddr) {
        return 0;
    }

    for (i = 0; i < 16; i++) {
        Seen[i] = 0;
    }

    if (HubGetDesc(C, HubAddr, Desc, 9)) {
        NPorts = Desc[2] ? Desc[2] : 4;
        if (NPorts > 14) {
            NPorts = 14;
        }
    }
    {
        char Dig[3];
        Dig[0] = (char)('0' + (NPorts % 10));
        Dig[1] = 0;
        ToyBootMarkUsb("Boot: EHCI hub ports=");
        ToyBootMarkUsb(Dig);
        ToyBootMarkUsb("\n");
    }

    /* note: aN:p… */
    if (NoteN < (int)sizeof(gEhciHubNote) - 4) {
        gEhciHubNote[NoteN++] = 'a';
        gEhciHubNote[NoteN++] = (char)('0' + (HubAddr % 10));
        gEhciHubNote[NoteN++] = ':';
    }

    for (P = 1; P <= NPorts; P++) {
        (void)HubSetFeat(C, HubAddr, P, HUB_FEAT_PORT_POWER);
    }
    /* 外接口上电常比内置 BT/摄像头慢 */
    EhciDelay(1500000);

    for (Pass = 0; Pass < 2; Pass++) {
        if (Pass == 1) {
            ToyBootMarkUsb("Boot: EHCI hub pass2\n");
            EhciDelay(1000000);
        }
        for (P = 1; P <= NPorts; P++) {
            UINT16 St;
            UINT16 Ch;
            if (Seen[P]) {
                continue;
            }
            if (!HubGetStatus(C, HubAddr, P, &St, &Ch)) {
                continue;
            }
            /* 无 CONNECT 也打 st，便于对照外接鼠落在哪口 */
            if ((St & HUB_STAT_CONNECT) == 0 && Pass == 0) {
                char Dig[3];
                Dig[0] = (char)('0' + (P % 10));
                Dig[1] = 0;
                ToyBootMarkUsb("Boot: EHCI hub+");
                ToyBootMarkUsb(Dig);
                MarkHex4(" st=", St);
                ToyBootMarkUsb("\n");
                continue;
            }
            if ((St & HUB_STAT_CONNECT) == 0) {
                continue;
            }
            Seen[P] = 1;
            if (NoteN < (int)sizeof(gEhciHubNote) - 6) {
                gEhciHubNote[NoteN++] = (char)('0' + (P % 10));
                gEhciHubNote[NoteN++] = ',';
            }
            if (TryHubPort(C, HubAddr, P)) {
                gEhciHubNote[NoteN] = 0;
                return 1;
            }
            if (gEhciLastErr && gEhciLastErr[0] == 't') {
                ToyBootMarkUsb("Boot: EHCI ");
                ToyBootMarkUsb(gEhciLastErr);
                ToyBootMarkUsb("\n");
            }
        }
    }
    if (NoteN > 0 && gEhciHubNote[NoteN - 1] == ',') {
        NoteN--;
    }
    gEhciHubNote[NoteN] = 0;
    if (!(gEhciLastErr && gEhciLastErr[0] == 't')) {
        gEhciLastErr = "hub no hid";
    }
    ToyBootMarkUsb("Boot: EHCI hub no hid\n");
    return 0;
}
