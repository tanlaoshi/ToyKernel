/*
 * InputXhci.c — x86 xHCI HID 输入（PR-D3：经 Driver Input 类注册；PR-H2：普查/软 IRQ）
 */
#include "Driver.h"
#include "DriverInput.h"
#include "Hal.h"
#include "ToySerialLog.h"
#include "PCIe.h"
#include "XHCI.h"
#include "Debug.h"
#include "VirtualMemory.h"

/* x86：MMIO 须 PCD|PWT，否则真机写 PORTSC/RS 易假死 */
#ifndef PTE_PWT
#define PTE_PWT (1ULL << 3)
#define PTE_PCD (1ULL << 4)
#endif
#define PTE_MMIO (PTE_PRESENT | PTE_WRITABLE | PTE_PWT | PTE_PCD)

static USB_CONTROLLER gXhciDev;
static int gXhciReady;

static void MapXhciBar(UINT64 Base) {
    UINT64 Start;
    UINT64 End;
    UINT32 Cap;
    UINT32 CapLength;
    UINT64 Op;
    UINT64 Db;
    UINT64 Rt;
    UINT64 Need;

    if (Base == 0) {
        return;
    }
    Start = Base & ~(UINT64)(4096 - 1);
    /* 先映 1 页读 Cap，再按 RTSOFF/DBOFF 扩；避免无脑 64MiB×逐页 TLB flush 假死 */
    VirtualMemoryMapPage(Start, Start, PTE_MMIO);
    Cap = *(volatile UINT32 *)(UINTN)Start;
    CapLength = Cap & 0xFF;
    if (Cap == 0xFFFFFFFFu || CapLength < 0x20 || CapLength == 0xFF) {
        End = Start + 0x100000ULL; /* 退化：1MiB */
    } else {
        Op = Start + CapLength;
        Db = Start + (UINT64)((*(volatile UINT32 *)(UINTN)(Start + 0x14)) & ~0x3u);
        Rt = Start + (UINT64)((*(volatile UINT32 *)(UINTN)(Start + 0x18)) & ~0x1Fu);
        Need = Op + 0x800; /* 端口寄存器区 */
        if (Db + 0x1000 > Need) {
            Need = Db + 0x1000;
        }
        if (Rt + 0x1000 > Need) {
            Need = Rt + 0x1000;
        }
        /* 上限 16MiB，防止异常偏移拖死 */
        if (Need > Start + 0x1000000ULL) {
            Need = Start + 0x1000000ULL;
        }
        End = (Need + 0xFFFULL) & ~0xFFFULL;
    }
    while (Start < End) {
        VirtualMemoryMapPage(Start, Start, PTE_MMIO);
        Start += 4096;
    }
}

static void XhciInputPoll(void) {
    /* 控制器已起就盲 Drain；勿等 MousePresent（曾因 deferred 鼠标导致 PHOTO d=1） */
    if (gXhciReady) {
        XhciDrainEvents();
    }
}

static int XhciKeyboardDequeue(HAL_KEYBOARD_REPORT *Report) {
    USB_KEYBOARD_REPORT Raw;

    if (!Report || !gXhciReady) {
        return 0;
    }
    if (!XhciDequeueKeyboard(&Raw)) {
        return 0;
    }
    Report->ModifierKeys = Raw.ModifierKeys;
    Report->Reserved = Raw.Reserved;
    for (int i = 0; i < 6; i++) {
        Report->KeyCode[i] = Raw.KeyCode[i];
    }
    return 1;
}

static int XhciInputSetLeds(UINT8 Leds) {
    if (!gXhciReady) {
        return -1;
    }
    return XhciKeyboardSetLeds(Leds);
}

static int XhciMousePresentWrap(void) {
    return gXhciReady ? XhciMousePresent() : 0;
}

static int XhciMouseDequeue(HAL_MOUSE_REPORT *Report) {
    USB_MOUSE_REPORT Raw;

    if (!Report || !gXhciReady) {
        return 0;
    }
    if (!XhciDequeueMouse(&Raw)) {
        return 0;
    }
    Report->X = Raw.X;
    Report->Y = Raw.Y;
    Report->Buttons = Raw.Buttons;
    Report->Wheel = Raw.Wheel;
    Report->Absolute = Raw.Absolute;
    return 1;
}

static const INPUT_BACKEND gXhciInputBackend = {
    .Poll = XhciInputPoll,
    .KeyboardDequeue = XhciKeyboardDequeue,
    .KeyboardSetLeds = XhciInputSetLeds,
    .MousePresent = XhciMousePresentWrap,
    .MouseDequeue = XhciMouseDequeue,
};

static int TryXhciAt(UINT64 Base, USB_CONTROLLER *Dev) {
    char B[20];
    int RealPc = !HalCpuIsHypervisor();

    if (Base == 0) {
        return 0;
    }
    /* 真机：Map 前 mute，避免 try BAR= 的 Present 卡死进不了 Init */
    if (RealPc) {
        HalSerialGopMute(1);
        ToyBootMarkUsb("boot: xhci-B10 map\n");
    }
    MapXhciBar(Base);
    if (RealPc) {
        ToyBootMarkUsb("boot: xhci-B10 mapped\n");
    } else {
        ToyLogUsb("boot: xhci try BAR=");
        HalSerialFormatHex(B, Base, 16);
        ToyLogUsb(B);
        ToyLogUsb("\n");
    }
    /* 拒绝明显非 MMIO 的 BAR（运行时误探曾出现 0x193A50） */
    if (Base < 0x100000ULL) {
        ToyLogUsb("boot: xhci skip low BAR\n");
        if (RealPc) {
            HalSerialGopMute(0);
        }
        return 0;
    }
    DebugWrite("XHCI: try BAR ");
    DebugHex64(Base);
    DebugWrite("\n");
    if (!XhciInit(Base)) {
        if (RealPc) {
            HalSerialGopMute(0);
        }
        return 0;
    }
    /*
     * 课堂：立刻开 MSI。真机：枚举阶段保持 IE=0，PHOTO 后再 InputXhciArmIrq，
     * 避免中断风暴把 boot 日志冲掉。
     */
    if (Dev && !RealPc && (XhciHidKeyboardReady() || XhciMousePresent())) {
        (void)XhciEnableIrq(Dev);
        if (!XhciUsesIrq()) {
            DebugWrite("XHCI: bound without IRQ (poll)\n");
        }
    }
    return 1;
}

static int XhciDriverProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPriv) {
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
        if (OutPriv) {
            *OutPriv = 0;
        }
        return 0;
    }
    /* xHCI MMIO 需在 VMM Enable 之后映射 */
    if (!VirtualMemoryEnabled()) {
        return -1;
    }

    Count = PciScanUSBControllers(Controllers, 8);
    ToyLogUsb("boot: xHCI controllers=");
    HalSerialFormatHex(B, (UINT64)(UINT32)Count, 2);
    ToyLogUsb(B);
    ToyLogUsb("\n");
    DebugWrite("XHCI: controllers=");
    DebugHex32((UINT32)Count);
    DebugWrite("\n");

    /* 刀：先列出所有 ProgIF=0x30（含 BAR），PHOTO 可抄 */
    XhciN = 0;
    for (i = 0; i < Count; i++) {
        if (Controllers[i].Type != 0x30) {
            continue;
        }
        {
            char Msg[72];
            int n = 0;
            const char *P = "boot: xhci#";
            while (*P && n < 12) {
                Msg[n++] = *P++;
            }
            Msg[n++] = (char)('0' + (XhciN % 10));
            P = " ";
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
            ToyBootMarkUsb(Msg);
            ToyLogUsb(Msg);
        }
        XhciN++;
    }
    {
        char Msg[28];
        int n = 0;
        const char *P = "boot: xHCI n=";
        while (*P) {
            Msg[n++] = *P++;
        }
        HalSerialFormatHex(B, (UINT64)(UINT32)XhciN, 1);
        Msg[n++] = B[2];
        Msg[n++] = '\n';
        Msg[n] = 0;
        ToyBootMarkUsb(Msg);
        ToyLogUsb(Msg);
    }

    XhciIdx = 0;
    for (i = 0; i < Count; i++) {
        if (Controllers[i].Type != 0x30) {
            continue;
        }
        {
            char Msg[24];
            int n = 0;
            const char *P = "boot: xhci try#";
            while (*P) {
                Msg[n++] = *P++;
            }
            Msg[n++] = (char)('0' + (XhciIdx % 10));
            Msg[n++] = '\n';
            Msg[n] = 0;
            ToyBootMarkUsb(Msg);
            ToyLogUsb(Msg);
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
            ToyLogUsb("boot: xhci init returned\n");
            if (XhciHidKeyboardReady() || XhciMousePresent()) {
                gXhciReady = 1;
                if (!XhciHidKeyboardReady() && XhciMousePresent()) {
                    ToyLogUsb("boot: xhci-hid mouse-only bind\n");
                }
                if (OutPriv) {
                    *OutPriv = 0;
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
            ToyLogUsb("boot: xhci up (no HID), try PS/2\n");
            break;
        }
        ToyLogUsb("boot: xhci init failed at BAR\n");
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
                ToyLogUsb("boot: xhci-hid keyboard\n");
                if (OutPriv) {
                    *OutPriv = 0;
                }
                return 0;
            }
            ToyLogUsb("boot: xhci fallback BAR failed / no HID\n");
        }
    }
    /* 不在此处再打 “no boot keyboard”——交给 PS/2 Probe 与 usb 模块汇总 */
    return -1;
}

static int XhciDriverBind(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
    return ToyDriverInputAttach(&gXhciInputBackend);
}

static void XhciDriverRemove(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
    gXhciReady = 0;
}

static const TOY_DRIVER gXhciInputDriver = {
    .Name = "xhci-hid",
    .Class = TOY_DRIVER_CLASS_INPUT,
    .Match = 0,
    .Probe = XhciDriverProbe,
    .Bind = XhciDriverBind,
    .Remove = XhciDriverRemove,
};

void InputXhciRegister(void) {
    (void)ToyDriverRegister(&gXhciInputDriver);
}

int InputXhciInit(void) {
    (void)ToyDriverProbeClass(TOY_DRIVER_CLASS_INPUT);
    return ToyDriverInputReady() ? 0 : -1;
}

/* 真机：PHOTO 后 Arm → EnableIrq 试 dual，失败 poll (fallback) */
void InputXhciArmIrq(void) {
    if (!gXhciReady) {
        return;
    }
    if (!(XhciHidKeyboardReady() || XhciMousePresent())) {
        return;
    }
    if (XhciUsesIrq()) {
        return;
    }
    (void)XhciEnableIrq(&gXhciDev);
    if (!XhciUsesIrq() && HalCpuIsHypervisor()) {
        ToyLogUsb("boot: xhci arm fallback poll\n");
    }
}
