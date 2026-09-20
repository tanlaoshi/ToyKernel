/*
 * XhciHubEnumMouse.c — PR-S-xhcihub-1：hub 子口鼠标枚举
 *
 * 从 XhciHub.c 原样搬家；不改语义。Hub 口帮手去掉 static，声明在 XhciInternal.h。
 */
#include "XHCI/XhciInternal.h"

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
        BootLogV("Boot: XHCI hub mouse port\n");
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
            BootLogV("Boot: XHCI hub mouse not PED\n");
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
                BootLogHexV("Boot: XHCI hub skip weak mouse score=", gMouseParseScore, 2);
                DisableSlot(gMouseSlotId);
                gMouseSlotId = 0;
                continue;
            }
            gMousePort = gHubRootPort;
            gMouseIface = Iface;
            BootLogHex("Boot: XHCI mouse hub ep=", EpAddr, 2);
            BootLogHex("Boot: XHCI mouse hub mps=", Mps, 2);
            BootLogHex("Boot: XHCI mouse hub iv=", Interval, 2);
            BootLogHex("Boot: XHCI mouse hub spd=", Speed, 1);
            BootLogHex("Boot: XHCI mouse hub score=", gMouseParseScore, 1);
            BootLogHex("Boot: XHCI mouse hub tt=",
                       ((UINT32)gMouseHubSlot << 8) | gMouseTtPort, 4);
            BootLogHex("Boot: XHCI mouse hub route=", gMouseRoute, 2);
            if (!ConfigureMouseIntr(gMouseSlotId, EpAddr, Mps, Interval, Speed)) {
                DisableSlot(gMouseSlotId);
                gMouseSlotId = 0;
                continue;
            }
            {
                UINT32 *EpOut = (UINT32 *)(void *)(gMouseDevCtx + gCtxSize * gMouseIntrDci);
                FlushDma(EpOut, gCtxSize);
                BootLogHex("Boot: XHCI mouse epst=", EpOut[0] & 7u, 1);
            }
            ZeroMemory(gMouseBuf, sizeof(gMouseBuf));
            QueueMouseIntr();
            BootLog("Boot: XHCI-HID Mouse Via Hub\n");
            return 1;
        }
    }
    return 0;
}

