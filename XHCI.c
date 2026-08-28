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
    int row = 130;

    Uint32ToHex((UINT32)(BaseAddress >> 32), buf);
    DrawString(10, row, "[XHCI] High:", COLOR_YELLOW);
    DrawString(200, row, buf, COLOR_WHITE);
    row += 20;

    Uint32ToHex((UINT32)BaseAddress, buf);
    DrawString(10, row, "[XHCI] Low:", COLOR_YELLOW);
    DrawString(200, row, buf, COLOR_WHITE);
    row += 20;

    UINT32 CapLength = mmio_read32(BaseAddress) & 0xFF;
    Uint32ToHex(CapLength, buf);
    DrawString(10, row, "CapLength:", COLOR_YELLOW);
    DrawString(200, row, buf, COLOR_WHITE);
    row += 20;

    if (CapLength < 0x20) {
        DrawString(10, row, "XHCI Init FAILED!", COLOR_RED);
        return 0;
    }

    UINT32 HCSParams1 = mmio_read32(BaseAddress + 0x04);
    UINT32 MaxPorts = HCSParams1 & 0xFF;
    Uint32ToHex(MaxPorts, buf);
    DrawString(10, row, "MaxPorts:", COLOR_YELLOW);
    DrawString(200, row, buf, COLOR_WHITE);
    row += 20;

    UINT64 OpBase = BaseAddress + CapLength;

    UINT32 UsbSts = mmio_read32(OpBase + 0x04);
    Uint32ToHex(UsbSts, buf);
    DrawString(10, row, "USBSTS:", COLOR_YELLOW);
    DrawString(200, row, buf, COLOR_WHITE);
    row += 20;

    if (UsbSts & 0x01) {
        DrawString(10, row, "Starting controller...", COLOR_YELLOW);
        row += 20;
        UINT32 UsbCmd = mmio_read32(OpBase + 0x00);
        mmio_write32(OpBase + 0x00, UsbCmd | 0x02);

        int timeout = 1000000;
        while (timeout--) {
            if (!(mmio_read32(OpBase + 0x04) & 0x01)) {
                DrawString(10, row, "Controller started!", COLOR_GREEN);
                row += 20;
                break;
            }
        }
    }

    // 端口状态 - 检测并初始化端口
    int deviceFound = 0;

    for (UINT32 i = 0; i < MaxPorts && i < 16; i++) {
        UINT32 PortOffset = 0x400 + i * 0x10;
        UINT32 PortSC = mmio_read32(OpBase + PortOffset);

        Uint32ToHex(PortSC, buf);
        DrawString(10, row, "PORTSC", COLOR_YELLOW);
        DrawString(120, row, buf, COLOR_WHITE);
        row += 20;

        if (PortSC & 0x01) {
            DrawString(10, row, "Device on port", COLOR_GREEN);
            char portNum[4];
            portNum[0] = '0' + i;
            portNum[1] = '\0';
            DrawString(200, row, portNum, COLOR_WHITE);
            row += 20;
            deviceFound = 1;
            
            // 启用端口 (PED = Bit 2)
            if (!(PortSC & 0x04)) {
                mmio_write32(OpBase + PortOffset, PortSC | 0x04);
                DrawString(10, row, "Port enabled", COLOR_YELLOW);
                row += 20;
            }
            
            // 复位端口 (PR = Bit 4)
            mmio_write32(OpBase + PortOffset, PortSC | 0x10);
            int wait = 100000;
            while (wait-- && (mmio_read32(OpBase + PortOffset) & 0x10));
            DrawString(10, row, "Port reset done", COLOR_YELLOW);
            row += 20;
            
            // 再次读取端口状态
            UINT32 PortSC2 = mmio_read32(OpBase + PortOffset);
            Uint32ToHex(PortSC2, buf);
            DrawString(10, row, "PORTSC after reset:", COLOR_YELLOW);
            DrawString(200, row, buf, COLOR_WHITE);
            row += 20;
            
            break;
        }
    }

    if (!deviceFound) {
        DrawString(10, row, "No device connected!", COLOR_RED);
        row += 20;
        DrawString(10, row, "Waiting for device...", COLOR_YELLOW);
        row += 20;
        
        // 持续等待设备连接
        while (1) {
            for (UINT32 i = 0; i < MaxPorts && i < 16; i++) {
                UINT32 PortOffset = 0x400 + i * 0x10;
                UINT32 PortSC = mmio_read32(OpBase + PortOffset);
                if (PortSC & 0x01) {
                    DrawString(10, row, "Device now connected!", COLOR_GREEN);
                    row += 20;
                    deviceFound = 1;
                    
                    // 启用端口
                    if (!(PortSC & 0x04)) {
                        mmio_write32(OpBase + PortOffset, PortSC | 0x04);
                        DrawString(10, row, "Port enabled", COLOR_YELLOW);
                        row += 20;
                    }
                    
                    // 复位端口
                    mmio_write32(OpBase + PortOffset, PortSC | 0x10);
                    int wait = 100000;
                    while (wait-- && (mmio_read32(OpBase + PortOffset) & 0x10));
                    DrawString(10, row, "Port reset done", COLOR_YELLOW);
                    row += 20;
                    
                    break;
                }
            }
            if (deviceFound) break;
            // 延时等待
            for (int delay = 0; delay < 1000000; delay++);
        }
    }

    DrawString(10, row, "XHCI Init SUCCESS!", COLOR_GREEN);
    row += 20;
    DrawString(10, row, "Polling for USB keyboard...", COLOR_YELLOW);

    return 1;
}

int XHCIInit(UINT32 BaseAddress) {
    return XHCIInit64((UINT64)BaseAddress);
}