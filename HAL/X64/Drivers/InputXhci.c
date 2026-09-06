/*
 * InputXhci.c — x86 xHCI HID 输入（PR-D3：经 Driver Input 类注册；PR-H2：普查/软 IRQ）
 */
#include "Driver.h"
#include "DriverInput.h"
#include "Hal.h"
#include "PCIe.h"
#include "XHCI.h"
#include "Debug.h"
#include "VirtualMemory.h"

static USB_CONTROLLER gXhciDev;
static int gXhciReady;

static void MapXhciBar(UINT64 Base) {
    UINT64 Start;
    UINT64 End;

    if (Base == 0) {
        return;
    }
    Start = Base & ~(UINT64)(4096 - 1);
    End = Start + 0x1000000ULL;
    while (Start < End) {
        VirtualMemoryMapPage(Start, Start, PTE_PRESENT | PTE_WRITABLE);
        Start += 4096;
    }
}

static void XhciInputPoll(void) {
    if (gXhciReady) {
        /* MSI 与否都排空；无 IRQ 时靠此收报告（PR-H2） */
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
    if (Base == 0) {
        return 0;
    }
    MapXhciBar(Base);
    DebugWrite("XHCI: try BAR ");
    DebugHex64(Base);
    DebugWrite("\n");
    if (!XhciInit(Base)) {
        return 0;
    }
    if (Dev) {
        (void)XhciEnableIrq(Dev);
        if (!XhciUsesIrq()) {
            DebugWrite("XHCI: bound without MSI (poll)\n");
        }
    }
    return 1;
}

static int XhciDriverProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPriv) {
    USB_CONTROLLER Controllers[8];
    int Count;
    int i;

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
    DebugWrite("XHCI: controllers=");
    DebugHex32((UINT32)Count);
    DebugWrite("\n");
    for (i = 0; i < Count; i++) {
        if (Controllers[i].Type != 0x30) {
            continue;
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
            gXhciReady = 1;
            HalSerialWrite("boot: xhci-hid keyboard\n");
            if (OutPriv) {
                *OutPriv = 0;
            }
            return 0;
        }
    }

    {
        UINT64 Fallback = HalPlatformXhciFallback();
        if (Fallback != 0) {
            gXhciDev.Bus = 0;
            gXhciDev.Device = 0;
            gXhciDev.Function = 0;
            gXhciDev.BaseAddress = Fallback;
            gXhciDev.Bar[0] = Fallback;
            gXhciDev.Type = 0x30;
            if (TryXhciAt(Fallback, 0)) {
                gXhciReady = 1;
                HalSerialWrite("boot: xhci-hid keyboard\n");
                if (OutPriv) {
                    *OutPriv = 0;
                }
                return 0;
            }
        }
    }
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
