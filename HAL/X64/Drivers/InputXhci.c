/*
 * InputXhci.c — x86 xHCI HID 输入（PR-D3：经 Driver Input 类注册）
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

static void XhciInputPoll(void) {
    if (gXhciReady && XhciUsesIrq()) {
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
    return 1;
}

static const INPUT_BACKEND gXhciInputBackend = {
    .Poll = XhciInputPoll,
    .KeyboardDequeue = XhciKeyboardDequeue,
    .KeyboardSetLeds = XhciInputSetLeds,
    .MousePresent = XhciMousePresentWrap,
    .MouseDequeue = XhciMouseDequeue,
};

static int XhciDriverProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPriv) {
    USB_CONTROLLER Controllers[8];
    int Count;
    int Found = 0;

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
    for (int i = 0; i < Count; i++) {
        if (Controllers[i].Type == 0x30) {
            gXhciDev = Controllers[i];
            Found = 1;
            break;
        }
    }
    if (!Found) {
        UINT64 Fallback = HalPlatformXhciFallback();
        if (Fallback == 0) {
            return -1;
        }
        gXhciDev.BaseAddress = Fallback;
        gXhciDev.Bar[0] = Fallback;
        gXhciDev.Type = 0x30;
    }
    if (!XhciInit(gXhciDev.BaseAddress)) {
        return -1;
    }
    if (!XhciEnableIrq(&gXhciDev)) {
        DebugWrite("XHCI: IRQ not enabled\n");
        return -1;
    }
    gXhciReady = 1;
    if (OutPriv) {
        *OutPriv = 0;
    }
    return 0;
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
