/*
 * XhciHub.c — PR-H-xhci-split-6：根口 hub / 子设备枚举
 *
 * 从单体 XHCI.c 原样搬家；不改语义。
 */
#include "XHCI/XhciInternal.h"

/* ---- PR-H-hub：一层 USB2 hub（根口 Class 9）---- */


int HubCtrl(UINT8 BmReq, UINT8 Req, UINT16 Value, UINT16 Index,
                   UINT16 Len, void *Data) {
    USB_SETUP_PACKET Setup = {
        .bmRequestType = BmReq,
        .bRequest = Req,
        .wValue = Value,
        .wIndex = Index,
        .wLength = Len
    };
    gXferSlot = gHubSlotId;
    return ControlXfer(&Setup, Data);
}

int FinishHubSetup(UINT8 *OutNumPorts) {
    UINT8 HubDesc[16];
    UINT8 Nports = 4;
    UINT8 ConfigVal = 1;

    if (GetDesc(0x0200, 0, 9, gCtrlBuf) < 0) {
        return 0;
    }
    ConfigVal = gCtrlBuf[5] ? gCtrlBuf[5] : 1;
    if (SetConfig(ConfigVal) < 0) {
        return 0;
    }
    ZeroMemory(HubDesc, sizeof(HubDesc));
    gHubTtt = 0;
    if (HubCtrl(0xA0, 0x06, 0x2900, 0, sizeof(HubDesc), HubDesc) == 0 &&
        HubDesc[2] != 0) {
        Nports = HubDesc[2];
        if (Nports > 15) {
            Nports = 15;
        }
        /* USB2 Hub Desc：wHubCharacteristics bit5-6 = TT Think Time */
        gHubTtt = (UINT8)((HubDesc[3] >> 5) & 3u);
    }
    if (OutNumPorts) {
        *OutNumPorts = Nports;
    }
    gHubNumPorts = Nports;
    BootLogV("boot: xhci hub ports ok\n");
    return 1;
}

static int HubGetPortStatus(UINT8 Port, UINT32 *OutSt) {
    UINT8 Buf[4];
    if (HubCtrl(0xA3, 0x00, 0, Port, 4, Buf) < 0) {
        return -1;
    }
    *OutSt = (UINT32)Buf[0] | ((UINT32)Buf[1] << 8) |
             ((UINT32)Buf[2] << 16) | ((UINT32)Buf[3] << 24);
    return 0;
}

static int HubSetPortFeat(UINT8 Port, UINT16 Feat) {
    return HubCtrl(0x23, 0x03, Feat, Port, 0, 0);
}

static int HubClearPortFeat(UINT8 Port, UINT16 Feat) {
    return HubCtrl(0x23, 0x01, Feat, Port, 0, 0);
}

/* hub 口速度：USB2 wPortStatus bits 10..9 → xHCI Port Speed 编码近似 */
static UINT8 HubPortSpeed(UINT32 St) {
    UINT32 Bits = (St >> 9) & 3u;
    if (Bits == 0) {
        return 1; /* full */
    }
    if (Bits == 1) {
        return 2; /* low */
    }
    if (Bits == 2) {
        return 3; /* high */
    }
    return 1;
}

int TryConfigureKeyboardSlot(UINT8 Speed) {
    UINT8 EpAddr = 0, Interval = 10;
    UINT16 Mps = 8;
    UINT8 ConfigVal = 1;
    int HaveIntr = 0;
    UINT16 Total;

    if (GetDesc(0x0200, 0, 9, gCtrlBuf) < 0) {
        return 0;
    }
    Total = (UINT16)(gCtrlBuf[2] | (gCtrlBuf[3] << 8));
    if (Total < 9) {
        Total = 9;
    }
    if (Total > sizeof(gCtrlBuf)) {
        Total = (UINT16)sizeof(gCtrlBuf);
    }
    if (GetDesc(0x0200, 0, Total, gCtrlBuf) < 0) {
        return 0;
    }
    ConfigVal = gCtrlBuf[5];
    if (ConfigVal == 0) {
        ConfigVal = 1;
    }
    HaveIntr = ParseConfig(gCtrlBuf, Total, Speed, &gKbdIface, &EpAddr, &Mps, &Interval);
    if (!HaveIntr) {
        EnumWhy("boot: why=no hid ep\n");
        return 0;
    }
    if (RealPcRejectMouseExtraAsKeyboard(Total, Speed)) {
        if (ClaimAddressedSlotAsMouse(gHubRootPort ? gHubRootPort : gPort1, Speed, Total,
                                      ConfigVal)) {
            /* gSlotId 已清；调用方 DisableSlot(0) 为空操作 */
            return 0;
        }
        return 0;
    }
    if (SetConfig(ConfigVal) < 0) {
        return 0;
    }
    (void)SetProtocolBoot(gKbdIface);
    SetIdle(gKbdIface);
    {
        UINT8 MEp = 0, MIv = 10;
        UINT16 MMps = 8;
        int WantMouse;

        /*
         * 真机 v6：复合键鼠上 Add 鼠标后即便 Sync ok 仍 k=0。
         * 对照实验：只配键盘、不 Prep/Add 鼠标（避免 Stall 与二次 Config）。
         * 独立口鼠标仍可由 InitMouseOnPort 绑定。
         */
        WantMouse = HalCpuIsHypervisor() &&
                    PrepCompositeMouse(Total, Speed, gKbdIface, EpAddr, &MEp, &MMps, &MIv);
        if (!HalCpuIsHypervisor()) {
            if (!ConfigureIntr(EpAddr, Mps, Interval, Speed, 0, 0, 0)) {
                return 0;
            }
            ZeroMemory(gReportBuf, 8);
            QueueIntr();
            BootLog("boot: xhci kbd-only then bind mouse ports\n");
            BootLog("boot: xhci kbd-fix=v8\n");
        } else if (WantMouse) {
            (void)SetInterface(gKbdIface, 0);
            (void)SetProtocolBoot(gKbdIface);
            SetIdle(gKbdIface);
            if (!ConfigureIntr(EpAddr, Mps, Interval, Speed, MEp, MMps, MIv)) {
                return 0;
            }
            ZeroMemory(gReportBuf, 8);
            QueueIntr();
            QueueMouseIntr();
            BootLog("boot: xhci-hid mouse (composite)\n");
        } else {
            if (!ConfigureIntr(EpAddr, Mps, Interval, Speed, 0, 0, 0)) {
                return 0;
            }
            ZeroMemory(gReportBuf, 8);
            QueueIntr();
        }
    }
    gUseGetReport = 0;
    return 1;
}

int IsHubDeviceDesc(void) {
    /* GET_DESCRIPTOR device 已在 gCtrlBuf */
    if (gCtrlBuf[4] == 0x09) {
        return 1;
    }
    return 0;
}

/* 配置描述符中是否有 Hub Interface（bDeviceClass=0 的常见 hub） */
int ConfigHasHubIface(UINT8 *Cfg, UINT16 Total) {
    UINT16 Off = 0;

    while (Off + 9 <= Total) {
        UINT8 Len = Cfg[Off];
        UINT8 Type = Cfg[Off + 1];
        if (Len < 2 || Off + Len > Total) {
            break;
        }
        if (Type == 4 && Len >= 9 && Cfg[Off + 5] == 0x09) {
            return 1;
        }
        Off = (UINT16)(Off + Len);
    }
    return 0;
}

int EnumHubChildrenForKeyboard(void) {
    UINT8 Port;
    UINT8 MaxP = gHubNumPorts;
    volatile int D;

    if (MaxP == 0 || MaxP > 15) {
        MaxP = 8;
    }
    for (Port = 1; Port <= MaxP; Port++) {
        UINT32 St = 0;
        UINT8 Speed;
        int t;

        if (HubSetPortFeat(Port, HUB_FEAT_PORT_POWER) < 0) {
            continue;
        }
        if (!HalCpuIsHypervisor()) {
            StallMs(100);
        } else {
            for (D = 0; D < 80000; D++) {
            }
        }
        if (HubGetPortStatus(Port, &St) < 0) {
            continue;
        }
        if (!(St & HUB_PORT_CONNECTION)) {
            continue;
        }
        BootLog("boot: xhci hub port connect\n");
        if (HubSetPortFeat(Port, HUB_FEAT_PORT_RESET) < 0) {
            continue;
        }
        for (t = 0; t < (HalCpuIsHypervisor() ? 50000 : 40); t++) {
            if (HubGetPortStatus(Port, &St) < 0) {
                break;
            }
            if (St & HUB_C_PORT_RESET) {
                (void)HubClearPortFeat(Port, HUB_FEAT_C_PORT_RESET);
                break;
            }
            if (!HalCpuIsHypervisor()) {
                StallMs(5);
            }
        }
        for (t = 0; t < (HalCpuIsHypervisor() ? 20000 : 40); t++) {
            if (HubGetPortStatus(Port, &St) < 0) {
                break;
            }
            if (St & HUB_C_PORT_CONNECTION) {
                (void)HubClearPortFeat(Port, HUB_FEAT_C_PORT_CONNECTION);
            }
            if (St & HUB_PORT_ENABLE) {
                break;
            }
            if (!HalCpuIsHypervisor()) {
                StallMs(5);
            }
        }
        if (!(St & HUB_PORT_ENABLE)) {
            continue;
        }
        Speed = HubPortSpeed(St);
        gSpeed = Speed;
        gPort1 = gHubRootPort;
        if (!AddressDeviceOnPort(gHubRootPort, Speed, &gSlotId, gDevCtx,
                                 (UINT32)Port, (UINT8)gHubSlotId, Port, 0, 0)) {
            DisableSlot(gSlotId);
            continue;
        }
        if (GetDeviceDesc() < 0) {
            DisableSlot(gSlotId);
            continue;
        }
        if (IsHubDeviceDesc()) {
            /* 不做二层 hub */
            DisableSlot(gSlotId);
            continue;
        }
        if (!TryConfigureKeyboardSlot(Speed)) {
            DisableSlot(gSlotId);
            continue;
        }
        BootLog("boot: xhci-hid via hub\n");
        return 1;
    }
    return 0;
}

/* hub 子口找独立鼠标（根口 composite 弱 HID 被跳过时） */
int EnumHubChildrenForMouse(void) {
    UINT8 Port;
    UINT8 MaxP = gHubNumPorts;

    if (gHubSlotId == 0 || gMouseSlotId != 0) {
        return 0;
    }
    if (MaxP == 0 || MaxP > 15) {
        MaxP = 8;
    }
    for (Port = 1; Port <= MaxP; Port++) {
        UINT32 St = 0;
        UINT8 Speed;
        volatile int D;

        (void)HubSetPortFeat(Port, HUB_FEAT_PORT_POWER);
        if (!HalCpuIsHypervisor()) {
            StallMs(100);
        } else {
            for (D = 0; D < 80000; D++) {
            }
        }
        if (HubGetPortStatus(Port, &St) < 0) {
            continue;
        }
        if (!(St & HUB_PORT_CONNECTION)) {
            continue;
        }
        BootLogV("boot: xhci hub mouse port\n");
        /* 跳过已占用为键盘的子口（同 route） */
        if ((gKbdRoute & 0xF) == (UINT32)Port && gSlotId != 0) {
            continue;
        }
        if (HubSetPortFeat(Port, HUB_FEAT_PORT_RESET) < 0) {
            continue;
        }
        {
            int t;
            for (t = 0; t < (HalCpuIsHypervisor() ? 50000 : 40); t++) {
                if (HubGetPortStatus(Port, &St) < 0) {
                    break;
                }
                if (St & HUB_C_PORT_RESET) {
                    (void)HubClearPortFeat(Port, HUB_FEAT_C_PORT_RESET);
                    break;
                }
                if (!HalCpuIsHypervisor()) {
                    StallMs(5);
                }
            }
            /* 复位完成后须等 PORT_ENABLE，否则 Address 后中断 IN 永不完成 → m=0 */
            for (t = 0; t < (HalCpuIsHypervisor() ? 20000 : 40); t++) {
                if (HubGetPortStatus(Port, &St) < 0) {
                    break;
                }
                if (St & HUB_C_PORT_CONNECTION) {
                    (void)HubClearPortFeat(Port, HUB_FEAT_C_PORT_CONNECTION);
                }
                if (St & HUB_PORT_ENABLE) {
                    break;
                }
                if (!HalCpuIsHypervisor()) {
                    StallMs(5);
                }
            }
        }
        if (!(St & HUB_PORT_ENABLE)) {
            BootLogV("boot: xhci hub mouse not PED\n");
            continue;
        }
        Speed = HubPortSpeed(St);
        if (!AddressDeviceOnPort(gHubRootPort, Speed, &gMouseSlotId, gMouseDevCtx,
                                 (UINT32)Port, (UINT8)gHubSlotId, Port, 0, 0)) {
            gMouseSlotId = 0;
            continue;
        }
        if (!SetupHidDevice(gMouseSlotId, gMouseDevCtx, Speed, ParseConfigMouse, 1)) {
            DisableSlot(gMouseSlotId);
            gMouseSlotId = 0;
            continue;
        }
        {
            UINT8 EpAddr = 0, Interval = 10, Iface = 0;
            UINT16 Mps = 8;
            UINT16 Total;
            if (GetDesc(0x0200, 0, 9, gCtrlBuf) < 0) {
                DisableSlot(gMouseSlotId);
                gMouseSlotId = 0;
                continue;
            }
            Total = (UINT16)(gCtrlBuf[2] | (gCtrlBuf[3] << 8));
            if (Total < 9) {
                Total = 9;
            }
            if (Total > sizeof(gCtrlBuf)) {
                Total = (UINT16)sizeof(gCtrlBuf);
            }
            if (GetDesc(0x0200, 0, Total, gCtrlBuf) < 0 ||
                !ParseConfigMouse(gCtrlBuf, Total, Speed, &Iface, &EpAddr, &Mps, &Interval)) {
                DisableSlot(gMouseSlotId);
                gMouseSlotId = 0;
                continue;
            }
            /*
             * 真机：拒绝弱 HID（score<2）。hub 上 U 盘/无线棒旁常有 vendor HID，
             * 误绑 → arms mouse=xx 但 PHOTO m=0；真鼠多在其它根口。
             */
            if (!HalCpuIsHypervisor() && gMouseParseScore < 2) {
                BootLogHexV("boot: xhci hub skip weak mouse score=", gMouseParseScore, 2);
                DisableSlot(gMouseSlotId);
                gMouseSlotId = 0;
                continue;
            }
            gMousePort = gHubRootPort;
            gMouseIface = Iface;
            BootLogHex("boot: xhci mouse hub ep=", EpAddr, 2);
            BootLogHex("boot: xhci mouse hub mps=", Mps, 2);
            BootLogHex("boot: xhci mouse hub iv=", Interval, 2);
            BootLogHex("boot: xhci mouse hub spd=", Speed, 1);
            BootLogHex("boot: xhci mouse hub score=", gMouseParseScore, 1);
            BootLogHex("boot: xhci mouse hub tt=",
                       ((UINT32)gMouseHubSlot << 8) | gMouseTtPort, 4);
            BootLogHex("boot: xhci mouse hub route=", gMouseRoute, 2);
            if (!ConfigureMouseIntr(gMouseSlotId, EpAddr, Mps, Interval, Speed)) {
                DisableSlot(gMouseSlotId);
                gMouseSlotId = 0;
                continue;
            }
            {
                UINT32 *EpOut = (UINT32 *)(void *)(gMouseDevCtx + gCtxSize * gMouseIntrDci);
                FlushDma(EpOut, gCtxSize);
                BootLogHex("boot: xhci mouse epst=", EpOut[0] & 7u, 1);
            }
            ZeroMemory(gMouseBuf, sizeof(gMouseBuf));
            QueueMouseIntr();
            BootLog("boot: xhci-hid mouse via hub\n");
            return 1;
        }
    }
    return 0;
}

/*
 * PR-H-msc-4：hub 子口找 MSC（Bulk）；跳过已占用键/鼠子口。
 * Address → XhciMscFinishClaim（SetConfig+Bulk，无 SCSI）。
 */
int EnumHubChildrenForMsc(void) {
    UINT8 Port;
    UINT8 MaxP = gHubNumPorts;

    if (gHubSlotId == 0 || gMscClaimed) {
        return 0;
    }
    if (MaxP == 0 || MaxP > 15) {
        MaxP = 8;
    }
    BootLog("boot: msc claim hub children\n");
    BootLogHex("boot: msc claim hub slot=", gHubSlotId, 2);
    BootLogHex("boot: msc claim hub nports=", MaxP, 2);
    for (Port = 1; Port <= MaxP; Port++) {
        UINT32 St = 0;
        UINT8 Speed;
        volatile int D;
        int t;

        (void)HubSetPortFeat(Port, HUB_FEAT_PORT_POWER);
        if (!HalCpuIsHypervisor()) {
            StallMs(20);
        } else {
            for (D = 0; D < 20000; D++) {
            }
        }
        if (HubGetPortStatus(Port, &St) < 0) {
            continue;
        }
        if (!(St & HUB_PORT_CONNECTION)) {
            continue;
        }
        if ((gKbdRoute & 0xF) == (UINT32)Port && gSlotId != 0) {
            continue;
        }
        if ((gMouseRoute & 0xF) == (UINT32)Port && gMouseSlotId != 0) {
            continue;
        }
        BootLogHex("boot: msc claim hub port=", Port, 2);
        if (HubSetPortFeat(Port, HUB_FEAT_PORT_RESET) < 0) {
            continue;
        }
        for (t = 0; t < (HalCpuIsHypervisor() ? 50000 : 40); t++) {
            if (HubGetPortStatus(Port, &St) < 0) {
                break;
            }
            if (St & HUB_C_PORT_RESET) {
                (void)HubClearPortFeat(Port, HUB_FEAT_C_PORT_RESET);
                break;
            }
            if (!HalCpuIsHypervisor()) {
                StallMs(5);
            }
        }
        for (t = 0; t < (HalCpuIsHypervisor() ? 20000 : 40); t++) {
            if (HubGetPortStatus(Port, &St) < 0) {
                break;
            }
            if (St & HUB_C_PORT_CONNECTION) {
                (void)HubClearPortFeat(Port, HUB_FEAT_C_PORT_CONNECTION);
            }
            if (St & HUB_PORT_ENABLE) {
                break;
            }
            if (!HalCpuIsHypervisor()) {
                StallMs(5);
            }
        }
        if (!(St & HUB_PORT_ENABLE)) {
            BootLogHex("boot: msc claim hub not en port=", Port, 2);
            continue;
        }
        Speed = HubPortSpeed(St);
        if (gMscScanSlot != 0) {
            DisableSlot(gMscScanSlot);
            gMscScanSlot = 0;
        }
        gMscRoute = (UINT32)Port;
        gMscHubSlot = (UINT8)gHubSlotId;
        gMscTtPort = Port;
        if (!AddressDeviceOnPort(gHubRootPort, Speed, &gMscScanSlot, gMscScanDevCtx,
                                 (UINT32)Port, (UINT8)gHubSlotId, Port, 0, 0)) {
            if (gMscScanSlot != 0) {
                DisableSlot(gMscScanSlot);
                gMscScanSlot = 0;
            }
            BootLogHex("boot: msc claim hub addr fail port=", Port, 2);
            continue;
        }
        if (gMscScanSlot <= DCBAA_SLOTS) {
            gSlotEp0UsesKbdRing[gMscScanSlot] = 0;
        }
        if (XhciMscFinishClaim(gHubRootPort, Speed)) {
            BootLog("boot: msc claim via hub\n");
            return 1;
        }
    }
    return 0;
}

/*
 * HID 已占用 gHubSlotId 时，外接第二 hub（ExistingSlot）带 U 盘：
 * 勿走 ClaimHubOnRootPort（会 DisableSlot 第二 hub）。临时切 gHub* 扫 MSC。
 */
int ProbeSecondHubForMsc(UINT32 HubSlot, UINT32 RootPort, UINT8 Speed) {
    UINT32 SavedSlot = gHubSlotId;
    UINT32 SavedRoot = gHubRootPort;
    UINT8 SavedPorts = gHubNumPorts;
    UINT8 SavedSpeed = gHubSpeed;
    UINT8 SavedMtt = gHubMtt;
    UINT8 SavedTtt = gHubTtt;
    UINT8 Nports = 4;
    int Usb2Hub = (Speed < 4);
    int Ok;

    if (HubSlot == 0 || HubSlot == gHubSlotId) {
        return EnumHubChildrenForMsc();
    }

    BootLogHex("boot: msc claim 2nd hub slot=", HubSlot, 2);
    BootLogHex("boot: msc claim 2nd hub root=", RootPort, 2);

    /* Ep0 走 msc 环，勿 InitRing(gHubEp0) 毁掉 HID hub dequeue */
    gMscProbeHubSlot = HubSlot;
    gHubSlotId = HubSlot;
    gHubRootPort = RootPort;
    gHubSpeed = Speed;
    gXferSlot = HubSlot;
    gEp0Mps = SpeedMps(Speed);
    RecoverEp0(HubSlot);
    HubNoteMttFromDevDesc(Speed);

    if (!FinishHubSetup(&Nports)) {
        BootLog("boot: msc claim 2nd hub cfg fail\n");
        DisableSlot(HubSlot);
        gMscProbeHubSlot = 0;
        gHubSlotId = SavedSlot;
        gHubRootPort = SavedRoot;
        gHubNumPorts = SavedPorts;
        gHubSpeed = SavedSpeed;
        gHubMtt = SavedMtt;
        gHubTtt = SavedTtt;
        return 0;
    }
    if (Usb2Hub && !EvaluateHubSlot(HubSlot, RootPort, Speed, Nports)) {
        BootLog("boot: msc claim 2nd hub eval skip\n");
    }

    Ok = EnumHubChildrenForMsc();
    gMscProbeHubSlot = 0;

    if (Ok) {
        BootLog("boot: msc claim 2nd hub keep parent\n");
        if (SavedSlot != 0) {
            gHubSlotId = SavedSlot;
            gHubRootPort = SavedRoot;
            gHubNumPorts = SavedPorts;
            gHubSpeed = SavedSpeed;
            gHubMtt = SavedMtt;
            gHubTtt = SavedTtt;
        }
        return 1;
    }

    BootLog("boot: msc claim 2nd hub no msc\n");
    DisableSlot(HubSlot);
    gHubSlotId = SavedSlot;
    gHubRootPort = SavedRoot;
    gHubNumPorts = SavedPorts;
    gHubSpeed = SavedSpeed;
    gHubMtt = SavedMtt;
    gHubTtt = SavedTtt;
    gMscRoute = 0;
    gMscHubSlot = 0;
    gMscTtPort = 0;
    return 0;
}

/*
 * 认领根口 hub：SetConfig + hub desc；USB2 再 Evaluate Hub/MTT。
 * ExistingSlot：InitMouseOnPort 已 Address 的 hub，保留 slot 勿 Disable+重 Address
 * （重 Address 带 Hub 位常 cc=0x11；USB3 hub 亦不可设 Hub 位）。
 * 不碰 gSlotId（键盘已绑定时可安全认领另一根口上的 hub）。
 */
int ClaimHubOnRootPort(UINT32 RootPort, UINT8 Speed, UINT32 ExistingSlot) {
    UINT8 Nports = 4;
    int Usb2Hub = (Speed < 4);

    if (gHubSlotId != 0) {
        if (ExistingSlot != 0 && ExistingSlot != gHubSlotId) {
            DisableSlot(ExistingSlot);
        }
        return 1;
    }
    BootLogV("boot: xhci claim hub\n");
    gHubRootPort = RootPort;
    gHubSpeed = Speed;
    /* Device Desc 多已在 gCtrlBuf；没有则补读再判 MTT */
    if (gCtrlBuf[4] != 0x09) {
        gXferSlot = ExistingSlot ? ExistingSlot : 0;
        if (ExistingSlot != 0) {
            gEp0Mps = SpeedMps(Speed);
            (void)GetDeviceDesc();
        }
    }
    HubNoteMttFromDevDesc(Speed);

    if (ExistingSlot != 0) {
        BootLogV("boot: xhci hub adopt slot\n");
        gHubSlotId = ExistingSlot;
        gXferSlot = ExistingSlot;
        gEp0Mps = SpeedMps(Speed);
        /* 该 slot 先前按 mouse 环 Address；迁到 hub 专用环，避免后续鼠 Address 踩坏 TT */
        RecoverEp0(gHubSlotId);
        if (!FinishHubSetup(&Nports)) {
            DisableSlot(gHubSlotId);
            gHubSlotId = 0;
            EnumWhy("boot: why=hub cfg\n");
            return 0;
        }
        if (Usb2Hub && !EvaluateHubSlot(gHubSlotId, RootPort, Speed, Nports)) {
            DisableSlot(gHubSlotId);
            gHubSlotId = 0;
            EnumWhy("boot: why=hub eval\n");
            return 0;
        }
        BootLogHex("boot: xhci hub spd=", Speed, 1);
        BootLog("boot: xhci ep0=split\n");
        return 1;
    }

    gEp0Mps = SpeedMps(Speed);
    if (!AddressDeviceOnPort(RootPort, Speed, &gHubSlotId, gHubDevCtx,
                             0, 0, 0, Usb2Hub ? 1 : 0, Usb2Hub ? 4 : 0)) {
        gHubSlotId = 0;
        EnumWhy("boot: why=hub addr\n");
        return 0;
    }
    /* Address 后才有 Device Desc → 再定 MTT，随后 Evaluate 写入 */
    if (GetDeviceDesc() == 0) {
        HubNoteMttFromDevDesc(Speed);
    }
    if (!FinishHubSetup(&Nports)) {
        DisableSlot(gHubSlotId);
        gHubSlotId = 0;
        return 0;
    }
    if (Usb2Hub && !EvaluateHubSlot(gHubSlotId, RootPort, Speed, Nports)) {
        BootLog("boot: xhci hub eval skip\n");
    }
    return 1;
}

/* 根口已 Address：device class=9 或配置含 hub iface → 枚举子口找键盘 */
int TryHubOnRootPort(UINT32 RootPort, UINT8 Speed) {
    BootLog("boot: xhci hub on root\n");
    /* 重新 Address 为 Hub 设备（带 Hub 位）；此时 gSlotId 是误 Address 的非 hub */
    DisableSlot(gSlotId);
    gSlotId = 0;
    if (!ClaimHubOnRootPort(RootPort, Speed, 0)) {
        return 0;
    }
    if (EnumHubChildrenForKeyboard()) {
        return 1;
    }
    return 0;
}


