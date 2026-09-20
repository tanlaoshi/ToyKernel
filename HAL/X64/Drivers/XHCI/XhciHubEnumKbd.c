/*
 * XhciHubEnumKbd.c — PR-S-xhcihub-1：hub 子口键盘枚举
 *
 * 从 XhciHub.c 原样搬家；不改语义。Hub 口帮手去掉 static，声明在 XhciInternal.h。
 */
#include "XHCI/XhciInternal.h"

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
        EnumWhy("Boot: Why=no hid ep\n");
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
            BootLog("Boot: XHCI kbd-only then bind mouse ports\n");
            BootLog("Boot: XHCI kbd-fix=v8\n");
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
            BootLog("Boot: XHCI-HID Mouse (Composite)\n");
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
        BootLog("Boot: XHCI hub port connect\n");
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
        BootLog("Boot: XHCI-HID Via Hub\n");
        return 1;
    }
    return 0;
}

