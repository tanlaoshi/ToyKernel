#include "Kernel.h"
#include "Video.h"
#include "UI.h"
#include "PCIe.h"

void KernelEntry(BOOT_CONFIG *BootConfig) {
    SetVideo(&BootConfig->VideoConfig);
    ClearScreen(COLOR_DARK_GRAY);
    
    UINT32 W = BootConfig->VideoConfig.HorizontalResolution;
    
    DrawString((W - 180) / 2, 10, "ToyOS - USB Keyboard Driver", COLOR_WHITE);
    
    USB_CONTROLLER Controllers[4];
    int Count = PciScanUSBControllers(Controllers, 4);
    
    if (Count > 0) {
        DrawString(10, 70 + Count * 20 + 30, "Step 1: PCI Scan - OK", COLOR_GREEN);
        DrawString(10, 70 + Count * 20 + 50, "Next: XHCI Initialization", COLOR_YELLOW);
    } else {
        DrawString(10, 100, "Try: -device nec-usb-xhci -device usb-kbd", COLOR_YELLOW);
    }
    
    while (1);
}