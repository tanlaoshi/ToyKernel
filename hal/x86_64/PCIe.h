/*
 * PCIe.h — PCI 配置空间与 USB 控制器扫描
 *
 * 通过 0xCF8/0xCFC 访问配置空间（底层走 HalIo）；支持 MSI/MSI-X。
 */
#ifndef PCIE_H
#define PCIE_H

#include "BootConfig.h"

typedef struct {
    UINT8  Bus;
    UINT8  Device;
    UINT8  Function;
    UINT8  Type;
    UINT64 BaseAddress;
    UINT64 Bar[6];
} USB_CONTROLLER;

char* Uint32ToHex(UINT32 Value);
char* Uint8ToDecimal(UINT8 Value);
char* Uint64ToHex(UINT64 Value);

UINT32 PciReadConfig(UINT8 Bus, UINT8 Device, UINT8 Function, UINT8 Offset);
void PciWriteConfig(UINT8 Bus, UINT8 Device, UINT8 Function, UINT8 Offset, UINT32 Value);
int PciScanUSBControllers(USB_CONTROLLER *Controllers, int MaxControllers);
int PciEnableMsi(USB_CONTROLLER *Device, UINT8 Vector);

#endif
