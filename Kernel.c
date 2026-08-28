#include "Kernel.h"
#include "Video.h"
#include "UI.h"
#include "PCIe.h"
#include "XHCI.h"

void KernelEntry(BOOT_CONFIG *BootConfig) {
    SetVideo(&BootConfig->VideoConfig);
    ClearScreen(COLOR_DARK_GRAY);

    UINT32 W = BootConfig->VideoConfig.HorizontalResolution;
    DrawString((W - 200) / 2, 10, "ToyOS - USB Keyboard Driver", COLOR_WHITE);

    char buf[32];
    UINT64 XhciBase = BootConfig->XhciBaseAddress;

    Uint32ToHex((UINT32)(XhciBase >> 32), buf);
    DrawString(10, 40, "[Kernel] XHCI High:", COLOR_YELLOW);
    DrawString(350, 40, buf, COLOR_WHITE);

    Uint32ToHex((UINT32)XhciBase, buf);
    DrawString(10, 60, "[Kernel] XHCI Low:", COLOR_YELLOW);
    DrawString(350, 60, buf, COLOR_WHITE);

    Uint32ToHex((UINT32)(XhciBase >> 32), buf);
    DrawString(10, 80, "[Kernel] Passing High:", COLOR_YELLOW);
    DrawString(350, 80, buf, COLOR_WHITE);

    Uint32ToHex((UINT32)XhciBase, buf);
    DrawString(10, 100, "[Kernel] Passing Low:", COLOR_YELLOW);
    DrawString(350, 100, buf, COLOR_WHITE);

    XHCIInit64(XhciBase);

    while (1);
}