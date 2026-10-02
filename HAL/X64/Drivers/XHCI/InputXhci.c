/*
 * InputXhci.c — xHCI HID 后端与试 BAR（PR-S3-inputxhci-1）
 *
 * Probe 普查见 InputXhciProbe.c。
 */
#include "Driver.h"
#include "DriverInput.h"
#include "Hal.h"
#include "ToySerialLog.h"
#include "XHCI.h"
#include "Debug.h"
#include "VirtualMemory.h"
#include "InputXhciPrivate.h"
#include "XHCI/XhciInternal.h"

/* x86：MMIO 须 PCD|PWT，否则真机写 PORTSC/RS 易假死 */
#ifndef PTE_PWT
#define PTE_PWT (1ULL << 3)
#define PTE_PCD (1ULL << 4)
#endif
#define PTE_MMIO (PTE_PRESENT | PTE_WRITABLE | PTE_PWT | PTE_PCD)

USB_CONTROLLER gXhciDev;
int gXhciReady;

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
        if (Db + XHCI_PAGE_SIZE > Need) {
            Need = Db + XHCI_PAGE_SIZE;
        }
        if (Rt + XHCI_PAGE_SIZE > Need) {
            Need = Rt + XHCI_PAGE_SIZE;
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

int TryXhciAt(UINT64 Base, USB_CONTROLLER *Dev) {
    int RealPc = !HalCpuIsHypervisor();

    if (Base == 0) {
        return 0;
    }
    /* 真机：Map 前 mute，避免 try BAR= 的 Present 卡死进不了 Init */
    if (RealPc) {
        HalSerialGopMute(1);
        ToyBootMarkUsb("Boot: XHCI-B10 map\n");
    }
    MapXhciBar(Base);
    if (RealPc) {
        ToyBootMarkUsb("Boot: XHCI-B10 mapped\n");
    } else {
        DebugWrite("XHCI: try BAR ");
        DebugHex64(Base);
        DebugWrite("\n");
    }
    /* 拒绝明显非 MMIO 的 BAR（运行时误探曾出现 0x193A50） */
    if (Base < 0x100000ULL) {
        ToyLogUsb("Boot: XHCI skip low BAR\n");
        if (RealPc) {
            HalSerialGopMute(0);
        }
        return 0;
    }
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
        ToyLogUsb("Boot: XHCI arm fallback poll\n");
    }
}
