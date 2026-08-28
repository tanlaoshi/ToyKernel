#include "PCIe.h"
#include "Video.h"
#include "UI.h"

#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC

UINT32 PciReadConfig(UINT8 Bus, UINT8 Device, UINT8 Function, UINT8 Offset) {
    UINT32 Address = (1 << 31) | (Bus << 16) | (Device << 11) | (Function << 8) | (Offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, Address);
    return inl(PCI_CONFIG_DATA);
}

void PciWriteConfig(UINT8 Bus, UINT8 Device, UINT8 Function, UINT8 Offset, UINT32 Value) {
    UINT32 Address = (1 << 31) | (Bus << 16) | (Device << 11) | (Function << 8) | (Offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, Address);
    outl(PCI_CONFIG_DATA, Value);
}

void Uint32ToHex(UINT32 Value, char *Buf) {
    Buf[0] = '0';
    Buf[1] = 'x';
    for (int i = 7; i >= 0; i--) {
        int Digit = (Value >> (i * 4)) & 0xF;
        Buf[9 - i] = (Digit < 10) ? ('0' + Digit) : ('A' + Digit - 10);
    }
    Buf[10] = '\0';
}

int PciScanUSBControllers(USB_CONTROLLER *Controllers, int MaxControllers) {
    int Count = 0;
    int Found = 0;
    
    DrawString(10, 50, "PCI Scanning...", COLOR_YELLOW);
    
    for (int Bus = 0; Bus < 256; Bus++) {
        for (int Device = 0; Device < 32; Device++) {
            for (int Function = 0; Function < 8; Function++) {
                UINT32 VendorDevice = PciReadConfig(Bus, Device, Function, 0x00);
                UINT16 VendorID = VendorDevice & 0xFFFF;
                
                if (VendorID == 0xFFFF) continue;
                
                UINT32 ClassCode = PciReadConfig(Bus, Device, Function, 0x08);
                UINT8 Class = (ClassCode >> 24) & 0xFF;
                UINT8 Subclass = (ClassCode >> 16) & 0xFF;
                UINT8 ProgIF = (ClassCode >> 8) & 0xFF;
                
                if (Class == 0x0C && Subclass == 0x03) {
                    if (Count >= MaxControllers) return Count;
                    
                    UINT32 Bar0 = PciReadConfig(Bus, Device, Function, 0x10);
                    UINT32 Bar1 = PciReadConfig(Bus, Device, Function, 0x14);
                    
                    // 启用内存空间
                    UINT32 Command = PciReadConfig(Bus, Device, Function, 0x04);
                    Command |= 0x02;
                    PciWriteConfig(Bus, Device, Function, 0x04, Command);
                    
                    UINT64 BaseAddress = Bar0 & 0xFFFFFFF0;
                    if ((Bar0 & 0x6) == 0x4) {
                        BaseAddress |= ((UINT64)Bar1 << 32);
                    }
                    
                    // 如果地址是 0，使用 QEMU 默认地址
                    if (BaseAddress == 0) {
                        BaseAddress = 0xFEB00000;
                    }
                    
                    Controllers[Count].BaseAddress = (UINT32)BaseAddress;
                    Controllers[Count].Type = ProgIF;
                    
                    char *TypeStr;
                    if (ProgIF == 0x00) TypeStr = "EHCI";
                    else if (ProgIF == 0x01) TypeStr = "OHCI";
                    else if (ProgIF == 0x03) TypeStr = "XHCI";
                    else TypeStr = "XHCI";
                    
                    char AddrBuf[16];
                    Uint32ToHex((UINT32)BaseAddress, AddrBuf);
                    
                    DrawString(10, 70 + Count * 20, "USB: ", COLOR_GREEN);
                    DrawString(60, 70 + Count * 20, TypeStr, COLOR_WHITE);
                    DrawString(130, 70 + Count * 20, AddrBuf, COLOR_CYAN);
                    
                    Count++;
                    Found = 1;
                }
            }
        }
    }
    
    if (!Found) {
        DrawString(10, 70, "No USB Controller found!", COLOR_RED);
    } else {
        DrawString(10, 70 + Count * 20 + 10, "PCI Scan Complete", COLOR_YELLOW);
    }
    
    return Count;
}