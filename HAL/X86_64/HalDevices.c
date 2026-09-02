/*
 * HalDevices.c — x86 USB 输入与 virtio-net HAL 门面
 */
#include "Hal.h"
#include "PCIe.h"
#include "XHCI.h"
#include "Net.h"
#include "Debug.h"

static USB_CONTROLLER gXhciDev;

int HalUsbInit(void) {
    USB_CONTROLLER Controllers[8];
    int Count = PciScanUSBControllers(Controllers, 8);
    int Found = 0;

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
    return 0;
}

void HalInputPoll(void) {
    if (XhciUsesIrq()) {
        XhciDrainEvents();
    }
}

int HalKeyboardDequeue(HAL_KEYBOARD_REPORT *Report) {
    USB_KEYBOARD_REPORT Raw;

    if (!Report) {
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

int HalKeyboardSetLeds(UINT8 Leds) {
    return XhciKeyboardSetLeds(Leds);
}

int HalMousePresent(void) {
    return XhciMousePresent();
}

int HalMouseDequeue(HAL_MOUSE_REPORT *Report) {
    USB_MOUSE_REPORT Raw;

    if (!Report) {
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

int HalNetInit(void) {
    return NetInit();
}

int HalNetReady(void) {
    return NetReady();
}

void HalNetPoll(void) {
    NetPoll();
}

void HalNetGetMac(UINT8 Mac[6]) {
    NetGetMac(Mac);
}

UINT32 HalNetGetIp(void) {
    return NetGetIp();
}

void HalNetFormatIp(UINT32 Ip, char *Buf, int BufLen) {
    NetFormatIp(Ip, Buf, BufLen);
}

int HalNetParseIp(const char *Text, UINT32 *Ip) {
    return NetParseIp(Text, Ip);
}

int HalNetPing(const char *Host, int TimeoutMs) {
    return NetPing(Host, TimeoutMs);
}

void HalNetGetStats(UINT32 *TxDone, UINT32 *RxFrames) {
    NetGetStats(TxDone, RxFrames);
}

int HalNetSendIp(UINT32 DstIp, UINT8 Proto, const void *Payload, UINTN PayloadLen) {
    return NetSendIp(DstIp, Proto, Payload, PayloadLen);
}

UINT16 HalNetChecksum(const void *Data, UINTN Len) {
    return NetChecksum(Data, Len);
}

void HalNetSetLwIpRx(int Enable) {
    NetSetLwIpRx(Enable);
}
