/*
 * IoApic.h — PR-H-ioapic：真机 I/O APIC 初始化与 GSI 路由（HAL/X64 内部）
 */
#ifndef IOAPIC_H
#define IOAPIC_H

#include "BootTypes.h"
#include "PCIe.h"

/* 解析 MADT、映 MMIO、掩全部 RTE；无 MADT 时默认 0xFEC00000。返回 0=就绪 */
int IoApicInit(void);

int IoApicReady(void);

/*
 * 将 GSI 路由到 Vector / DestApicId。
 * Level!=0 → 电平触发；ActiveLow!=0 → 低有效。
 * 返回 0=成功。
 */
int IoApicRouteGsi(UINT32 Gsi, UINT8 Vector, UINT8 DestApicId,
                   int Level, int ActiveLow);

/* ISA IRQ →（经 ISO）GSI；默认边沿高有效，ISO 可改 */
int IoApicRouteIsaIrq(UINT8 IsaIrq, UINT8 Vector, UINT8 DestApicId);

/* PCI INTx：读 Interrupt Line + ISO；电平低有效（无 ISO 时） */
int IoApicRoutePciIntx(USB_CONTROLLER *Device, UINT8 Vector, UINT8 DestApicId);

void IoApicMaskGsi(UINT32 Gsi, int Mask);

#endif
