/*
 * XhciMscClaimPort.c — PR-S-xhcimsc-1：单口 Force / Address / 根口 hub。
 * 从 XhciMscClaimPorts 嵌套块原样抽出；不改语义。
 * MSC 全局仍定义在 Xhci.c（BSS 顺序影响 HID DMA 环；勿迁出）。
 */
#include "XHCI/XhciInternal.h"

/*
 * Force PR 直到 PED+CCS（真机）；QEMU 且已 PED 则直过。
 * 成功 1 并写 *Force；失败 0。
 */
int MscClaimForceUntilPed(UINT32 P, int *Force) {
    UINT32 Ps;
    int Ready = 0;

    *Force = 0;
    Ps = ReadMmio32(gOperationalBase + PortReg(P));
    if (HalCpuIsHypervisor() && (Ps & PORTSC_PED) && (Ps & PORTSC_CCS)) {
        BootLogHex("Boot: MSC claim try PED port=", P, 2);
        return 1;
    }

    *Force = 1;
    BootLogHex("Boot: MSC claim reset force port=", P, 2);
    {
        int Attempt;

        for (Attempt = 0; Attempt < 3 && !Ready; Attempt++) {
            if (Attempt > 0) {
                int W;
                BootLogHex("Boot: MSC claim Force retry port=", P, 2);
                /* 丢 CCS 后等设备重新出现（Force 过猛常见） */
                for (W = 0; W < 50; W++) {
                    Ps = ReadMmio32(gOperationalBase + PortReg(P));
                    if (Ps & PORTSC_CCS) {
                        break;
                    }
                    StallMs(20);
                }
            }
            if (!ResetPortEx(P, 1)) {
                Ps = ReadMmio32(gOperationalBase + PortReg(P));
                if ((Ps & PORTSC_CCS) && !(Ps & PORTSC_PED)) {
                    int W;
                    /* PRC 已到但 PED 慢：再等一会，勿立刻放弃 */
                    for (W = 0; W < 50; W++) {
                        StallMs(20);
                        Ps = ReadMmio32(gOperationalBase + PortReg(P));
                        if ((Ps & PORTSC_PED) && (Ps & PORTSC_CCS)) {
                            Ready = 1;
                            BootLogHex("Boot: MSC claim late PED port=", P, 2);
                            break;
                        }
                        if (!(Ps & PORTSC_CCS)) {
                            break;
                        }
                    }
                }
                continue;
            }
            Ready = 1;
            if (!HalCpuIsHypervisor()) {
                StallMs(100);
            }
        }
    }
    if (!Ready) {
        BootLogHex("Boot: MSC claim reset fail port=", P, 2);
        return 0;
    }
    Ps = ReadMmio32(gOperationalBase + PortReg(P));
    if (!(Ps & PORTSC_PED) || !(Ps & PORTSC_CCS)) {
        BootLogHex("Boot: MSC claim not PED port=", P, 2);
        return 0;
    }
    return 1;
}

/*
 * AddressDeviceOnPort + QEMU/真机 Force 重试。
 * 成功 1；失败续扫 0；cmd sick 中止 -1。写出 *Speed。
 */
int MscClaimAddressPort(UINT32 P, int Force, UINT8 *Speed) {
    int AddrOk;
    UINT32 QuietSave;
    UINT32 Ps;

    Ps = ReadMmio32(gOperationalBase + PortReg(P));
    *Speed = PortSpeed(Ps);
    QuietSave = gDiagQuiet;
    gDiagQuiet = 1;
    AddrOk = AddressDeviceOnPort(P, *Speed, &gMscScanSlot, gMscScanDevCtx, 0, 0, 0, 0,
                                 0);
    if (!AddrOk && gMscScanSlot != 0) {
        DisableSlot(gMscScanSlot);
        gMscScanSlot = 0;
    }
    /* QEMU PED 直 Address 失败：Force 再试（真机本轮已 Force） */
    if (!AddrOk && !Force) {
        if (gXhciCmdSick) {
            gDiagQuiet = QuietSave;
            BootLog("Boot: MSC claim abort (cmd sick after PED Address)\n");
            RecoverCommandRing();
            gXhciCmdSick = 0;
            return -1;
        }
        BootLogHex("Boot: MSC claim addr retry Force port=", P, 2);
        if (ResetPortEx(P, 1)) {
            if (!HalCpuIsHypervisor()) {
                StallMs(100);
            }
            *Speed = PortSpeed(ReadMmio32(gOperationalBase + PortReg(P)));
            AddrOk = AddressDeviceOnPort(P, *Speed, &gMscScanSlot, gMscScanDevCtx,
                                         0, 0, 0, 0, 0);
            if (!AddrOk && gMscScanSlot != 0) {
                DisableSlot(gMscScanSlot);
                gMscScanSlot = 0;
            }
        }
    }
    /* 真机 Address 仍失败（cc=0x04/0x11）：再 Force+Address 一轮 */
    if (!AddrOk && Force && !HalCpuIsHypervisor() &&
        (gCmdCode == 4 || gCmdCode == 0x11) && !gXhciCmdSick) {
        BootLogHex("Boot: MSC claim addr 2nd Force port=", P, 2);
        if (gMscScanSlot != 0) {
            DisableSlot(gMscScanSlot);
            gMscScanSlot = 0;
        }
        StallMs(50);
        if (ResetPortEx(P, 1)) {
            StallMs(150);
            *Speed = PortSpeed(ReadMmio32(gOperationalBase + PortReg(P)));
            AddrOk = AddressDeviceOnPort(P, *Speed, &gMscScanSlot, gMscScanDevCtx,
                                         0, 0, 0, 0, 0);
            if (!AddrOk && gMscScanSlot != 0) {
                DisableSlot(gMscScanSlot);
                gMscScanSlot = 0;
            }
        }
    }
    gDiagQuiet = QuietSave;
    if (!AddrOk) {
        BootLogHex("Boot: MSC claim addr fail port=", P, 2);
        BootLogHex("Boot: MSC claim addr cc=", gCmdCode, 2);
        gPortNeedForcePr |= (1u << P);
        if (gXhciCmdSick) {
            BootLog("Boot: MSC claim abort (cmd sick)\n");
            return -1;
        }
        return 0;
    }
    if (gMscScanSlot <= DCBAA_SLOTS) {
        gSlotEp0UsesKbdRing[gMscScanSlot] = 0;
    }
    gPortNeedForcePr &= ~(1u << P);
    gMscRoute = 0;
    gMscHubSlot = 0;
    gMscTtPort = 0;
    return 1;
}

/*
 * 根口 hub 分支。成功认领 MSC：*Ok=1 且返回 1（goto done）。
 * 非 hub：返回 -1（继续 FinishClaim）。其它：返回 0（continue）。
 */
int MscClaimTryHubOnRoot(UINT32 P, UINT8 Speed, int *Ok) {
    int Hubish = IsHubDeviceDesc() || (gCtrlBuf[4] == 0x09);

    if (!Hubish && gCtrlBuf[4] == 0) {
        UINT16 Total;

        if (GetDesc(0x0200, 0, 9, gMscCfgBuf) == 0) {
            Total = (UINT16)(gMscCfgBuf[2] | (gMscCfgBuf[3] << 8));
            if (Total < 9) {
                Total = 9;
            }
            if (Total > sizeof(gMscCfgBuf)) {
                Total = (UINT16)sizeof(gMscCfgBuf);
            }
            if (GetDesc(0x0200, 0, Total, gMscCfgBuf) == 0 &&
                ConfigHasHubIface(gMscCfgBuf, Total)) {
                Hubish = 1;
                BootLogHex("Boot: MSC claim hub iface root=", P, 2);
            }
        } else {
            RecoverEp0(gMscScanSlot);
            gXferSlot = gMscScanSlot;
        }
    }

    if (!Hubish) {
        return -1;
    }

    {
        UINT32 Was = gMscScanSlot;
        UINT32 HubBefore = gHubSlotId;

        gMscScanSlot = 0;
        BootLogHex("Boot: MSC claim hub on root=", P, 2);
        /*
         * 已有 HID hub 时 ClaimHubOnRootPort 会 DisableSlot(Was)，
         * 正是外接第二 hub（U 盘所在）→ none + 长时间 Stall 像卡死。
         */
        if (HubBefore != 0 && Was != 0 && Was != HubBefore) {
            if (ProbeSecondHubForMsc(Was, P, Speed)) {
                *Ok = 1;
                return 1;
            }
            return 0;
        }
        if (ClaimHubOnRootPort(P, Speed, Was)) {
            if (!HalCpuIsHypervisor()) {
                StallMs(150);
            }
            if (EnumHubChildrenForMsc()) {
                *Ok = 1;
                return 1;
            }
            /* 空 hub：再扫一轮后再决定是否释放（U 盘上电慢） */
            if (!HalCpuIsHypervisor()) {
                StallMs(300);
                if (EnumHubChildrenForMsc()) {
                    *Ok = 1;
                    return 1;
                }
            }
            /* HID 未占用此 hub：无 MSC 则放掉，试其它根口 */
            if (HubBefore == 0 && gHubSlotId != 0 && gHubRootPort == P) {
                DisableSlot(gHubSlotId);
                gHubSlotId = 0;
                gHubRootPort = 0;
                BootLog("Boot: MSC claim hub no msc, release\n");
            }
        } else if (Was != 0) {
            DisableSlot(Was);
        }
        return 0;
    }
}

