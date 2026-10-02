/*
 * XhciEnumPort.c — 根口试键盘 / 绑鼠标（PR-F-xhci-1）
 */
#include "XHCI/XhciInternal.h"

/* 1 = 本口已作键盘；0 = 继续下一口 */
int XhciEnumTryRootPort(UINT32 P) {
    UINT8 Speed = 0;
    UINT8 EpAddr = 0, Interval = 10;
    UINT16 Mps = 8;
    UINT8 ConfigVal = 1;
    int HaveIntr = 0;
    UINT16 Total = 0;
    UINT32 Ps = ReadMmio32(gOperationalBase + PortReg(P));

    if (!(Ps & PORTSC_CCS)) {
        return 0;
    }
    BootLogHexV("Boot: XHCI try port=", P, 2);
    if (!ResetPort(P)) {
        return 0;
    }
    {
        UINT32 After = ReadMmio32(gOperationalBase + PortReg(P));
        Speed = PortSpeed(After);
    }
    gPort1 = P;
    gSpeed = Speed;

    BootMarkV("Boot: XHCI address...\n");
    if (!AddressDevice(P, Speed)) {
        ToyBootMarkUsb("Boot: XHCI addr fail\n");
        gPortNeedForcePr |= (1u << P);
        DisableSlot(gSlotId);
        return 0;
    }
    BootMarkV("Boot: XHCI address ok\n");

    BootMarkV("Boot: XHCI get desc\n");
    if (GetDeviceDesc() < 0) {
        ToyBootMarkUsb("Boot: XHCI desc fail\n");
        gPortNeedForcePr |= (1u << P);
        DisableSlot(gSlotId);
        return 0;
    }
    /* PR-H-hub：根口 hub（device class 9）→ 子口找键盘 */
    if (IsHubDeviceDesc()) {
        BootLog("Boot: XHCI hub root\n");
        if (TryHubOnRootPort(P, Speed)) {
            return 1;
        }
        EnumWhy("Boot: Why=hub fail\n");
        DisableSlot(gHubSlotId);
        return 0;
    }
    if (GetDesc(USB_WVALUE_DT_CONFIG, 0, 9, gCtrlBuf) < 0) {
        EnumWhy("Boot: Why=cfg desc\n");
        DisableSlot(gSlotId);
        return 0;
    }
    Total = (UINT16)(gCtrlBuf[2] | (gCtrlBuf[3] << 8));
    if (Total < 9) {
        Total = 9;
    }
    if (Total > sizeof(gCtrlBuf)) {
        Total = (UINT16)sizeof(gCtrlBuf);
    }
    if (GetDesc(USB_WVALUE_DT_CONFIG, 0, Total, gCtrlBuf) < 0) {
        EnumWhy("Boot: Why=cfg desc\n");
        DisableSlot(gSlotId);
        return 0;
    }
    /*
     * bDeviceClass=0 的 hub：配置里 Interface Class=9。
     * 家侧 port3「no hid ep」即此类；不进 hub 则真鼠标可能在 hub 后。
     */
    if (ConfigHasHubIface(gCtrlBuf, Total)) {
        BootLog("Boot: XHCI hub (iface class 9)\n");
        if (TryHubOnRootPort(P, Speed)) {
            return 1;
        }
        EnumWhy("Boot: Why=hub iface fail\n");
        DisableSlot(gHubSlotId);
        return 0;
    }
    ConfigVal = gCtrlBuf[5];
    if (ConfigVal == 0) {
        ConfigVal = 1;
    }
    HaveIntr = ParseConfig(gCtrlBuf, Total, Speed, &gKbdIface, &EpAddr, &Mps, &Interval);
    if (!HaveIntr) {
        EnumWhy("Boot: Why=no hid ep\n");
        gPortNoHid |= (1u << P);
        gPortNeedForcePr |= (1u << P);
        DisableSlot(gSlotId);
        return 0;
    }
    if (RealPcRejectMouseExtraAsKeyboard(Total, Speed)) {
        /* 优先原地认领为鼠；失败才 Disable，留给 InitMouseOnPort+ForcePR */
        if (ClaimAddressedSlotAsMouse(P, Speed, Total, ConfigVal)) {
            return 0;
        }
        gPortNeedForcePr |= (1u << P);
        DisableSlot(gSlotId);
        return 0;
    }
    if (SetConfig(ConfigVal) < 0) {
        EnumWhy("Boot: Why=set cfg\n");
        DisableSlot(gSlotId);
        return 0;
    }
    (void)SetProtocolBoot(gKbdIface);
    SetIdle(gKbdIface);
    {
        UINT8 MEp = 0, MIv = 10;
        UINT16 MMps = 8;
        int WantMouse;

        WantMouse = HalCpuIsHypervisor() &&
                    PrepCompositeMouse(Total, Speed, gKbdIface, EpAddr, &MEp, &MMps, &MIv);
        if (!HalCpuIsHypervisor()) {
            /* 真机 v6：仅键盘对照；复合鼠会弄死键 IN（v5 Sync ok 仍 k=0） */
            if (!ConfigureIntr(EpAddr, Mps, Interval, Speed, 0, 0, 0)) {
                DisableSlot(gSlotId);
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
                DisableSlot(gSlotId);
                return 0;
            }
            ZeroMemory(gReportBuf, 8);
            QueueIntr();
            QueueMouseIntr();
            BootLog("Boot: XHCI-HID Mouse (Composite)\n");
        } else {
            if (!ConfigureIntr(EpAddr, Mps, Interval, Speed, 0, 0, 0)) {
                DisableSlot(gSlotId);
                return 0;
            }
            ZeroMemory(gReportBuf, 8);
            QueueIntr();
        }
    }
    gUseGetReport = 0;
    return 1;
}

void XhciEnumBindMouseAfterKbd(void) {
    /*
     * 真机有线键鼠：优先其它口独立鼠（两 slot，利于保键盘）。
     * 刀 #117：#115 延到桌面后再 EnumHubChildrenForMouse 会 Reset hub 子口，
     * MSC 认盘后的二次枚举易把键鼠/中断弄死 →「进桌面不动」。改回枚举期绑。
     */
    if (gMouseSlotId == 0 && gHubSlotId != 0) {
        (void)EnumHubChildrenForMouse();
    }
    if (gMouseSlotId == 0) {
        for (UINT32 P = 1; P <= gMaxPorts; P++) {
            if (P == gPort1) {
                continue;
            }
            if (InitMouseOnPort(P)) {
                BootLog("Boot: XHCI mouse on other port\n");
                break;
            }
        }
    }
    if (gMouseSlotId == 0) {
        BootLog("Boot: XHCI mouse fallback composite-on-kbd\n");
        (void)InitMouseOnKeyboardSlot();
    }
    if (gMouseSlotId == 0 && gHubSlotId != 0) {
        (void)EnumHubChildrenForMouse();
    }
}
