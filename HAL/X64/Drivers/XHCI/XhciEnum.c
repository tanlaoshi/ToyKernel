/*
 * XhciEnum.c — PR-S-xhci-1：根口枚举键盘并绑鼠标
 *
 * 从 XhciInit 原样抽出；不改语义。BSS 仍在 Xhci.c。
 */
#include "XHCI/XhciInternal.h"

int XhciEnumAndBind(void) {
    int RealPc = !HalCpuIsHypervisor();
    UINT32 Port1 = 0;
    UINT8 Speed = 0;
    UINT8 EpAddr = 0, Interval = 10;
    UINT16 Mps = 8;
    UINT8 ConfigVal = 1;
    int HaveIntr = 0;
    UINT16 Total = 0;

    gPortNoHid = 0;
    gPortNeedForcePr = 0;
    {
        int PassMax = 3;
        for (int Wait = 0; Wait < PassMax && Port1 == 0; Wait++) {
            if (DiagVerbose()) {
                ToyLogUsb("Boot: XHCI enum pass=");
                {
                    char B[12];
                    HalSerialFormatHex(B, (UINT64)(UINT32)(Wait + 1), 2);
                    ToyLogUsb(B);
                    ToyLogUsb("\n");
                }
            }
            for (UINT32 p = 1; p <= gMaxPorts && p <= 32; p++) {
                UINT32 Ps = ReadMmio32(gOperationalBase + PortReg(p));
                if (!(Ps & PORTSC_CCS)) {
                    continue;
                }
                BootLogHexV("Boot: XHCI try port=", p, 2);
                if (!ResetPort(p)) {
                    continue;
                }
                UINT32 After = ReadMmio32(gOperationalBase + PortReg(p));
                Speed = PortSpeed(After);
                gPort1 = p;
                gSpeed = Speed;

                BootMarkV("Boot: XHCI address...\n");
                if (!AddressDevice(p, Speed)) {
                    ToyBootMarkUsb("Boot: XHCI addr fail\n");
                    gPortNeedForcePr |= (1u << p);
                    DisableSlot(gSlotId);
                    continue;
                }
                BootMarkV("Boot: XHCI address ok\n");

                BootMarkV("Boot: XHCI get desc\n");
                if (GetDeviceDesc() < 0) {
                    ToyBootMarkUsb("Boot: XHCI desc fail\n");
                    gPortNeedForcePr |= (1u << p);
                    DisableSlot(gSlotId);
                    continue;
                }
                /* PR-H-hub：根口 hub（device class 9）→ 子口找键盘 */
                if (IsHubDeviceDesc()) {
                    BootLog("Boot: XHCI hub root\n");
                    if (TryHubOnRootPort(p, Speed)) {
                        Port1 = gPort1;
                        break;
                    }
                    EnumWhy("Boot: Why=hub fail\n");
                    DisableSlot(gHubSlotId);
                    continue;
                }
                if (GetDesc(0x0200, 0, 9, gCtrlBuf) < 0) {
                    EnumWhy("Boot: Why=cfg desc\n");
                    DisableSlot(gSlotId);
                    continue;
                }
                {
                    Total = (UINT16)(gCtrlBuf[2] | (gCtrlBuf[3] << 8));
                    if (Total < 9) {
                        Total = 9;
                    }
                    if (Total > sizeof(gCtrlBuf)) {
                        Total = (UINT16)sizeof(gCtrlBuf);
                    }
                    if (GetDesc(0x0200, 0, Total, gCtrlBuf) < 0) {
                        EnumWhy("Boot: Why=cfg desc\n");
                        DisableSlot(gSlotId);
                        continue;
                    }
                    /*
                     * bDeviceClass=0 的 hub：配置里 Interface Class=9。
                     * 家侧 port3「no hid ep」即此类；不进 hub 则真鼠标可能在 hub 后。
                     */
                    if (ConfigHasHubIface(gCtrlBuf, Total)) {
                        BootLog("Boot: XHCI hub (iface class 9)\n");
                        if (TryHubOnRootPort(p, Speed)) {
                            Port1 = gPort1;
                            break;
                        }
                        EnumWhy("Boot: Why=hub iface fail\n");
                        DisableSlot(gHubSlotId);
                        continue;
                    }
                    ConfigVal = gCtrlBuf[5];
                    if (ConfigVal == 0) {
                        ConfigVal = 1;
                    }
                    HaveIntr = ParseConfig(gCtrlBuf, Total, Speed, &gKbdIface, &EpAddr, &Mps,
                                           &Interval);
                }
                if (!HaveIntr) {
                    EnumWhy("Boot: Why=no hid ep\n");
                    gPortNoHid |= (1u << p);
                    gPortNeedForcePr |= (1u << p);
                    DisableSlot(gSlotId);
                    continue;
                }
                if (RealPcRejectMouseExtraAsKeyboard(Total, Speed)) {
                    /* 优先原地认领为鼠；失败才 Disable，留给 InitMouseOnPort+ForcePR */
                    if (ClaimAddressedSlotAsMouse(p, Speed, Total, ConfigVal)) {
                        continue;
                    }
                    gPortNeedForcePr |= (1u << p);
                    DisableSlot(gSlotId);
                    continue;
                }
                if (SetConfig(ConfigVal) < 0) {
                    EnumWhy("Boot: Why=set cfg\n");
                    DisableSlot(gSlotId);
                    continue;
                }
                (void)SetProtocolBoot(gKbdIface);
                SetIdle(gKbdIface);
                {
                    UINT8 MEp = 0, MIv = 10;
                    UINT16 MMps = 8;
                    int WantMouse;

                    WantMouse = HalCpuIsHypervisor() &&
                                PrepCompositeMouse(Total, Speed, gKbdIface, EpAddr, &MEp, &MMps,
                                                   &MIv);
                    if (!HalCpuIsHypervisor()) {
                        /* 真机 v6：仅键盘对照；复合鼠会弄死键 IN（v5 Sync ok 仍 k=0） */
                        if (!ConfigureIntr(EpAddr, Mps, Interval, Speed, 0, 0, 0)) {
                            DisableSlot(gSlotId);
                            continue;
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
                            continue;
                        }
                        ZeroMemory(gReportBuf, 8);
                        QueueIntr();
                        QueueMouseIntr();
                        BootLog("Boot: XHCI-HID Mouse (Composite)\n");
                    } else {
                        if (!ConfigureIntr(EpAddr, Mps, Interval, Speed, 0, 0, 0)) {
                            DisableSlot(gSlotId);
                            continue;
                        }
                        ZeroMemory(gReportBuf, 8);
                        QueueIntr();
                    }
                }
                gUseGetReport = 0;
                Port1 = p;
                break;
            }
            if (Port1 == 0) {
                if (RealPc) {
                    StallMs(50);
                } else {
                    for (volatile int d = 0; d < 40000; d++) {
                    }
                }
            }
        }
    }

    if (Port1 == 0) {
        if (RealPc) {
            HalSerialGopMute(0); /* 放弃 xHCI：允许后续 boot 黄字 */
        }
        ToyLogUsb("Boot: XHCI up but no HID keyboard\n");
        BootLog("Boot: XHCI up but no HID keyboard\n");
        if (gEnumWhy) {
            BootLog(gEnumWhy);
        }
        DebugWrite("XHCI: no keyboard\n");
        for (UINT32 p = 1; p <= gMaxPorts && p <= 32; p++) {
            if (InitMouseOnPort(p)) {
                BootLog("Boot: XHCI mouse only\n");
                break;
            }
        }
        gXhciStarted = 1;
        return 1;
    }

    DebugWrite("XHCI: keyboard ready\n");
    /* 与 mouse 同走 BootLog：真机屏上先 keyboard 再 mouse，再由 Probe 打 init returned */
    BootLog("Boot: XHCI-HID Keyboard\n");

    /*
     * 真机有线键鼠：优先其它口独立鼠（两 slot，利于保键盘）。
     * 若独立口失败，再回退同 slot 复合——保证现在能用的鼠标不丢。
     */
    if (gMouseSlotId == 0 && gHubSlotId != 0) {
        (void)EnumHubChildrenForMouse();
    }
    if (gMouseSlotId == 0) {
        for (UINT32 p = 1; p <= gMaxPorts; p++) {
            if (p == gPort1) {
                continue;
            }
            if (InitMouseOnPort(p)) {
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

    gXhciStarted = 1;
    return 1;
}
