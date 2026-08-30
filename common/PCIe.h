/*
 * PCIe.h — PCI 配置空间与 USB 控制器扫描
 *
 * 通过 0xCF8/0xCFC 端口访问配置空间，支持 MSI/MSI-X 中断配置。
 */
#ifndef PCIE_H
#define PCIE_H

#include "BootConfig.h"

/* 扫描到的 USB 主机控制器信息 */
typedef struct {
    UINT8  Bus;
    UINT8  Device;
    UINT8  Function;
    UINT8  Type;          /* ProgIF: 0x30=XHCI */
    UINT64 BaseAddress;
    UINT64 Bar[6];
} USB_CONTROLLER;

static inline UINT32 inl(UINT16 Port) {
    UINT32 Value;
    __asm__ volatile("inl %1, %0" : "=a"(Value) : "Nd"(Port));
    return Value;
}

static inline void outl(UINT16 Port, UINT32 Value) {
    __asm__ volatile("outl %0, %1" : : "a"(Value), "Nd"(Port));
}

char* Uint32ToHex(UINT32 Value);
char* Uint8ToDecimal(UINT8 Value);
char* Uint64ToHex(UINT64 Value);

UINT32 PciReadConfig(UINT8 Bus, UINT8 Device, UINT8 Function, UINT8 Offset);
void PciWriteConfig(UINT8 Bus, UINT8 Device, UINT8 Function, UINT8 Offset, UINT32 Value);
int PciScanUSBControllers(USB_CONTROLLER *Controllers, int MaxControllers);
int PciEnableMsi(USB_CONTROLLER *Dev, UINT8 Vector);

#endif
