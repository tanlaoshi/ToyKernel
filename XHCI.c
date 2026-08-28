#include "XHCI.h"
#include "Video.h"
#include "UI.h"
#include "PCIe.h"

static inline UINT32 mmio_read32(UINT64 addr) {
    return *(volatile UINT32*)(UINTN)addr;
}

static inline void mmio_write32(UINT64 addr, UINT32 value) {
    *(volatile UINT32*)(UINTN)addr = value;
}

int XHCIInit64(UINT64 BaseAddress) {
    char buf[32];

    Uint32ToHex((UINT32)(BaseAddress >> 32), buf);
    DrawString(10, 130, "[XHCI] High:", COLOR_YELLOW);
    DrawString(200, 130, buf, COLOR_WHITE);

    Uint32ToHex((UINT32)BaseAddress, buf);
    DrawString(10, 150, "[XHCI] Low:", COLOR_YELLOW);
    DrawString(200, 150, buf, COLOR_WHITE);

    UINT32 CapLength = mmio_read32(BaseAddress) & 0xFF;
    Uint32ToHex(CapLength, buf);
    DrawString(10, 170, "CapLength:", COLOR_YELLOW);
    DrawString(200, 170, buf, COLOR_WHITE);

    if (CapLength < 0x20) {
        DrawString(10, 190, "XHCI Init FAILED!", COLOR_RED);
        return 0;
    }

    UINT32 HCSParams1 = mmio_read32(BaseAddress + 0x04);
    UINT32 MaxPorts = HCSParams1 & 0xFF;
    Uint32ToHex(MaxPorts, buf);
    DrawString(10, 190, "MaxPorts:", COLOR_YELLOW);
    DrawString(200, 190, buf, COLOR_WHITE);

    UINT64 OpBase = BaseAddress + CapLength;

    UINT32 UsbSts = mmio_read32(OpBase + 0x04);
    Uint32ToHex(UsbSts, buf);
    DrawString(10, 210, "USBSTS:", COLOR_YELLOW);
    DrawString(200, 210, buf, COLOR_WHITE);

    if (UsbSts & 0x01) {
        DrawString(10, 230, "Starting controller...", COLOR_YELLOW);
        UINT32 UsbCmd = mmio_read32(OpBase + 0x00);
        mmio_write32(OpBase + 0x00, UsbCmd | 0x02);

        int timeout = 1000000;
        while (timeout--) {
            if (!(mmio_read32(OpBase + 0x04) & 0x01)) {
                DrawString(10, 250, "Controller started!", COLOR_GREEN);
                break;
            }
        }
    }

    // 端口状态
    for (UINT32 i = 0; i < MaxPorts && i < 4; i++) {
        UINT32 PortOffset = 0x400 + i * 0x10;
        UINT32 PortSC = mmio_read32(OpBase + PortOffset);

        Uint32ToHex(PortSC, buf);
        DrawString(10, 270 + i * 20, "PORTSC", COLOR_YELLOW);
        DrawString(120, 270 + i * 20, buf, COLOR_WHITE);

        if (PortSC & 0x01) {
            DrawString(10, 270 + i * 20 + 10, "Device Connected!", COLOR_GREEN);
        }
    }

    DrawString(10, 350, "XHCI Init SUCCESS!", COLOR_GREEN);
    DrawString(10, 370, "Polling for USB keyboard...", COLOR_YELLOW);

    // 轮询检测键盘连接
    int found = 0;
    for (int loop = 0; loop < 1000000; loop++) {
        for (UINT32 i = 0; i < 4; i++) {
            UINT32 PortOffset = 0x400 + i * 0x10;
            UINT32 PortSC = mmio_read32(OpBase + PortOffset);
            
            if (PortSC & 0x01) {
                found = 1;
                char buf2[32];
                Uint32ToHex(PortSC, buf2);
                DrawString(10, 390 + i * 20, "Keyboard on port", COLOR_GREEN);
                DrawString(250, 390 + i * 20, buf2, COLOR_WHITE);
                break;
            }
        }
        if (found) break;
    }

    if (!found) {
        DrawString(10, 390, "No keyboard detected!", COLOR_RED);
        DrawString(10, 410, "Check QEMU: -device usb-kbd", COLOR_YELLOW);
    }

    return 1;
}

int XHCIInit(UINT32 BaseAddress) {
    return XHCIInit64((UINT64)BaseAddress);
}