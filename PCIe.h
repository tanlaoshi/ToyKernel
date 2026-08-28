#ifndef PCIE_H
#define PCIE_H

#include "BootConfig.h"

typedef struct {
    UINT32 BaseAddress;
    UINT32 Length;
    UINT8 Type;
} USB_CONTROLLER;

static inline UINT32 inl(UINT16 Port) {
    UINT32 Value;
    __asm__ volatile("inl %1, %0" : "=a"(Value) : "d"(Port));
    return Value;
}

static inline void outl(UINT16 Port, UINT32 Value) {
    __asm__ volatile("outl %0, %1" : : "a"(Value), "d"(Port));
}

UINT32 PciReadConfig(UINT8 Bus, UINT8 Device, UINT8 Function, UINT8 Offset);
void PciWriteConfig(UINT8 Bus, UINT8 Device, UINT8 Function, UINT8 Offset, UINT32 Value);
int PciScanUSBControllers(USB_CONTROLLER *Controllers, int MaxControllers);
void Uint32ToHex(UINT32 Value, char *Buf);  // 添加这行

#endif