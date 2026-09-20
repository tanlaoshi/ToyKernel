/*
 * XhciHubEnumMsc.c — PR-S-xhcihub-1：hub 子口 MSC 枚举与第二 hub
 *
 * 从 XhciHub.c 原样搬家；不改语义。Hub 口帮手去掉 static，声明在 XhciInternal.h。
 */
#include "XHCI/XhciInternal.h"

/*
 * PR-H-msc-4：hub 子口找 MSC（Bulk）；跳过已占用键/鼠子口。
 * Address → XhciMscFinishClaim（SetConfig+Bulk，无 SCSI）。
 * 真机：先统一上电再扫，空口多等几轮（U 盘上电慢 → 误判 hub empty）。
 */
int EnumHubChildrenForMsc(void) {
    UINT8 Port;
    UINT8 MaxP = gHubNumPorts;
    int Pass;

    if (gHubSlotId == 0 || gMscClaimed) {
        return 0;
    }
    if (MaxP == 0 || MaxP > 15) {
        MaxP = 8;
    }
    BootLog("Boot: MSC claim hub children\n");
    BootLogHex("Boot: MSC claim hub slot=", gHubSlotId, 2);
    BootLogHex("Boot: MSC claim hub nports=", MaxP, 2);

    /* Pass0：全部上电；Pass1+：读状态并认领 */
    for (Port = 1; Port <= MaxP; Port++) {
        (void)HubSetPortFeat(Port, HUB_FEAT_PORT_POWER);
    }
    if (!HalCpuIsHypervisor()) {
        StallMs(250);
    } else {
        volatile int D;
        for (D = 0; D < 80000; D++) {
        }
    }

    for (Pass = 0; Pass < 3; Pass++) {
        if (Pass > 0 && !HalCpuIsHypervisor()) {
            BootLogHex("Boot: MSC claim hub rescan pass=", (UINT32)Pass, 1);
            StallMs(200);
        }
        for (Port = 1; Port <= MaxP; Port++) {
            UINT32 St = 0;
            UINT8 Speed;
            int t;
            int Wait;

            if (HubGetPortStatus(Port, &St) < 0) {
                BootLogHex("Boot: MSC claim hub status fail port=", Port, 2);
                continue;
            }
            if (!(St & HUB_PORT_CONNECTION)) {
                /* 晚到的 CCS：多读几次再判 empty */
                for (Wait = 0; Wait < 8 && !(St & HUB_PORT_CONNECTION); Wait++) {
                    if (!HalCpuIsHypervisor()) {
                        StallMs(40);
                    }
                    if (HubGetPortStatus(Port, &St) < 0) {
                        break;
                    }
                }
                if (!(St & HUB_PORT_CONNECTION)) {
                    if (Pass == 2) {
                        BootLogHex("Boot: MSC claim hub empty port=", Port, 2);
                    }
                    continue;
                }
            }
            if ((gKbdRoute & 0xF) == (UINT32)Port && gSlotId != 0) {
                BootLogHex("Boot: MSC claim hub skip kbd port=", Port, 2);
                continue;
            }
            if ((gMouseRoute & 0xF) == (UINT32)Port && gMouseSlotId != 0) {
                BootLogHex("Boot: MSC claim hub skip mouse port=", Port, 2);
                continue;
            }
            BootLogHex("Boot: MSC claim hub port=", Port, 2);
            BootLogHex("Boot: MSC claim hub st=", St, 4);
            if (HubSetPortFeat(Port, HUB_FEAT_PORT_RESET) < 0) {
                continue;
            }
            for (t = 0; t < (HalCpuIsHypervisor() ? 50000 : 80); t++) {
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
            for (t = 0; t < (HalCpuIsHypervisor() ? 20000 : 80); t++) {
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
                BootLogHex("Boot: MSC claim hub not en port=", Port, 2);
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
                BootLogHex("Boot: MSC claim hub addr fail port=", Port, 2);
                /* Address 失败：子口再 reset 一次后本 Pass 继续下一口 */
                (void)HubSetPortFeat(Port, HUB_FEAT_PORT_RESET);
                continue;
            }
            if (gMscScanSlot <= DCBAA_SLOTS) {
                gSlotEp0UsesKbdRing[gMscScanSlot] = 0;
            }
            if (XhciMscFinishClaim(gHubRootPort, Speed)) {
                BootLog("Boot: MSC claim via hub\n");
                return 1;
            }
            if (gMscScanSlot != 0) {
                DisableSlot(gMscScanSlot);
                gMscScanSlot = 0;
            }
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

    BootLogHex("Boot: MSC claim 2nd hub slot=", HubSlot, 2);
    BootLogHex("Boot: MSC claim 2nd hub root=", RootPort, 2);

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
        BootLog("Boot: MSC claim 2nd hub cfg fail\n");
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
        BootLog("Boot: MSC claim 2nd hub eval skip\n");
    }

    Ok = EnumHubChildrenForMsc();
    gMscProbeHubSlot = 0;

    if (Ok) {
        BootLog("Boot: MSC claim 2nd hub keep parent\n");
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

    BootLog("Boot: MSC claim 2nd hub no msc\n");
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

