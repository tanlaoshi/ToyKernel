/*
 * InputXhciProbe.c — xHCI HID 控制器普查 / Probe（PR-S3-inputxhci-1）
 */
#include "Driver.h"
#include "Hal.h"
#include "ToySerialLog.h"
#include "PCIe.h"
#include "XHCI.h"
#include "Debug.h"
#include "VirtualMemory.h"
#include "InputXhciPrivate.h"

int XhciDriverProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPrivate) {
    USB_CONTROLLER Controllers[8];
    int Count;
    int i;
    int XhciN;
    int XhciIdx;
    int RealPc = !HalCpuIsHypervisor();
    char B[20];

    (void)Self;
    (void)BusCtx;
    if (gXhciReady) {
        if (OutPrivate) {
            *OutPrivate = 0;
        }
        return 0;
    }
    /* xHCI MMIO 需在 VMM Enable 之后映射 */
    if (!VirtualMemoryEnabled()) {
        return -1;
    }

    Count = PciScanUSBControllers(Controllers, 8);
    DebugWrite("XHCI: controllers=");
    DebugHex32((UINT32)Count);
    DebugWrite("\n");
    /* 真机 PHOTO 要抄 BAR；QEMU 默认安静 */
    if (RealPc) {
        ToyLogUsb("Boot: XHCI Controllers=");
        HalSerialFormatHex(B, (UINT64)(UINT32)Count, 2);
        ToyLogUsb(B);
        ToyLogUsb("\n");
    }

    /* 刀：先列出所有 ProgIF=0x30（含 BAR），PHOTO 可抄 */
    XhciN = 0;
    for (i = 0; i < Count; i++) {
        if (Controllers[i].Type != 0x30) {
            continue;
        }
        if (RealPc) {
            char Msg[72];
            int n = 0;
            const char *P = "Boot: XHCI#";
            while (*P && n < 12) {
                Msg[n++] = *P++;
            }
            Msg[n++] = (char)('0' + (XhciN % 10));
            Msg[n++] = ' ';
            HalSerialFormatHex(B, Controllers[i].Bus, 2);
            Msg[n++] = B[2];
            Msg[n++] = B[3];
            Msg[n++] = ':';
            HalSerialFormatHex(B, Controllers[i].Device, 2);
            Msg[n++] = B[2];
            Msg[n++] = B[3];
            Msg[n++] = '.';
            HalSerialFormatHex(B, Controllers[i].Function, 1);
            Msg[n++] = B[2];
            P = " bar=";
            while (*P && n < 40) {
                Msg[n++] = *P++;
            }
            HalSerialFormatHex(B, Controllers[i].BaseAddress, 16);
            {
                int j = 0;
                while (B[j] && n < 70) {
                    Msg[n++] = B[j++];
                }
            }
            Msg[n++] = '\n';
            Msg[n] = 0;
            ToyBootMarkUsb(Msg); /* 已含 UART，勿再 ToyLogUsb 双打 */
        }
        XhciN++;
    }
    if (RealPc) {
        char Msg[28];
        int n = 0;
        const char *P = "Boot: XHCI N=";
        while (*P) {
            Msg[n++] = *P++;
        }
        HalSerialFormatHex(B, (UINT64)(UINT32)XhciN, 1);
        Msg[n++] = B[2];
        Msg[n++] = '\n';
        Msg[n] = 0;
        ToyBootMarkUsb(Msg);
    }

    XhciIdx = 0;
    for (i = 0; i < Count; i++) {
        if (Controllers[i].Type != 0x30) {
            continue;
        }
        if (RealPc) {
            char Msg[24];
            int n = 0;
            const char *P = "Boot: XHCI try#";
            while (*P) {
                Msg[n++] = *P++;
            }
            Msg[n++] = (char)('0' + (XhciIdx % 10));
            Msg[n++] = '\n';
            Msg[n] = 0;
            ToyBootMarkUsb(Msg);
        }
        DebugWrite("XHCI: pci ");
        DebugHex32(Controllers[i].Bus);
        DebugWrite(":");
        DebugHex32(Controllers[i].Device);
        DebugWrite(".");
        DebugHex32(Controllers[i].Function);
        DebugWrite("\n");
        gXhciDev = Controllers[i];
        if (TryXhciAt(Controllers[i].BaseAddress, &gXhciDev)) {
            if (RealPc) {
                ToyLogUsb("Boot: XHCI init returned\n");
            }
            if (XhciHidKeyboardReady() || XhciMousePresent()) {
                gXhciReady = 1;
                if (!XhciHidKeyboardReady() && XhciMousePresent()) {
                    ToyLogUsb("Boot: XHCI-HID Mouse-Only Bind\n");
                }
                if (OutPrivate) {
                    *OutPrivate = 0;
                }
                return 0; /* Bind USB HID */
            }
            /*
             * 真机：无 HID（常见 CCS=0）→ abandon 再试下一颗 xHCI。
             * QEMU/单控制器：保持旧行为，立刻让出给 PS/2。
             */
            if (RealPc && XhciIdx + 1 < XhciN) {
                XhciAbandonNoHid();
                XhciIdx++;
                continue;
            }
            ToyLogUsb("Boot: XHCI up (no HID), try PS/2\n");
            break;
        }
        ToyLogUsb("Boot: XHCI init failed at BAR\n");
        if (RealPc && XhciIdx + 1 < XhciN) {
            XhciIdx++;
            continue;
        }
        if (!HalCpuIsHypervisor()) {
            break;
        }
        XhciIdx++;
    }

    {
        UINT64 Fallback = HalPlatformXhciFallback();
        /* 真机勿二次 Init（同 BAR 再 reset 会挂） */
        if (Fallback != 0 && HalCpuIsHypervisor()) {
            gXhciDev.Bus = 0;
            gXhciDev.Device = 0;
            gXhciDev.Function = 0;
            gXhciDev.BaseAddress = Fallback;
            gXhciDev.Bar[0] = Fallback;
            gXhciDev.Type = 0x30;
            if (TryXhciAt(Fallback, &gXhciDev) &&
                (XhciHidKeyboardReady() || XhciMousePresent())) {
                gXhciReady = 1;
                ToyLogUsb("Boot: XHCI-HID Keyboard\n");
                if (OutPrivate) {
                    *OutPrivate = 0;
                }
                return 0;
            }
            ToyLogUsb("Boot: XHCI fallback BAR failed / no HID\n");
        }
    }
    /* 不在此处再打 “no boot keyboard”——交给 PS/2 Probe 与 usb 模块汇总 */
    return -1;
}
