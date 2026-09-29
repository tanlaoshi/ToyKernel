/*
 * PCIeMsi.c — PCI MSI/MSI-X 与 IOAPIC INTx（PR-S3-pcie-1）
 */
#include "PCIe.h"
#include "Debug.h"
#include "Hal.h"
#include "IoApic.h"
#include "VirtualMemory.h"

#ifndef PTE_PWT
#define PTE_PWT (1ULL << 3)
#define PTE_PCD (1ULL << 4)
#endif
#define PTE_MMIO (PTE_PRESENT | PTE_WRITABLE | PTE_PWT | PTE_PCD)

static void MapMmioPages(UINT64 Phys, UINTN Bytes) {
    UINT64 Start = Phys & ~(UINT64)(4096 - 1);
    UINT64 End = (Phys + Bytes + 4095) & ~(UINT64)(4096 - 1);
    while (Start < End) {
        VirtualMemoryMapPage(Start, Start, PTE_MMIO);
        Start += 4096;
    }
}

int PciFindCap(UINT8 Bus, UINT8 Device, UINT8 Function, UINT8 Id) {
    UINT32 Status = PciReadConfig(Bus, Device, Function, 0x04);
    if (!((Status >> 16) & 0x10)) {
        return 0;
    }
    UINT32 Ptr = PciReadConfig(Bus, Device, Function, 0x34) & 0xFC;
    int Guard = 0;
    while (Ptr && Guard++ < 48) {
        UINT32 Val = PciReadConfig(Bus, Device, Function, (UINT8)Ptr);
        if ((Val & 0xFF) == Id) {
            return (int)Ptr;
        }
        Ptr = (Val >> 8) & 0xFC;
    }
    return 0;
}

/* 为 USB 设备配置 MSI-X（优先）或 MSI，绑定到 LAPIC 向量 Vector。
 * DestLogicalCpu：投递目标逻辑核（PR-S-input-irq：xHCI 传输入核；其它可传 0=BSP） */
int PciEnableMsi(USB_CONTROLLER *Device, UINT8 Vector, UINT8 DestLogicalCpu) {
    UINT32 Cmd;
    UINT8 DestApic;
    int Cap;

    if (!Device) {
        return 0;
    }

    /* INTx Disable：改走 MSI/MSI-X */
    Cmd = PciReadConfig(Device->Bus, Device->Device, Device->Function, 0x04);
    Cmd |= (1u << 10);
    PciWriteConfig(Device->Bus, Device->Device, Device->Function, 0x04, Cmd);

    DestApic = HalCpuApicId(DestLogicalCpu);

    Cap = PciFindCap(Device->Bus, Device->Device, Device->Function, 0x11);
    if (Cap) {
        UINT32 TableDw = PciReadConfig(Device->Bus, Device->Device, Device->Function, (UINT8)(Cap + 4));
        UINT32 Bir = TableDw & 7;
        UINT32 Off = TableDw & 0xFFFFFFF8u;
        UINT32 Dw0;
        UINT32 TableSize;
        UINT32 i;
        UINT64 Table;
        UINT32 MsgAddr;

        if (Bir > 5 || Device->Bar[Bir] == 0) {
            /* H4e-3：BAR 未填全时勿直接失败，回落经典 MSI */
            DebugWrite("MSI-X: bad BIR, try MSI\n");
        } else {
        Dw0 = PciReadConfig(Device->Bus, Device->Device, Device->Function, (UINT8)Cap);
        TableSize = ((Dw0 >> 16) & 0x7FFu) + 1u;
        if (TableSize > 64u) {
            TableSize = 64u;
        }
        Table = Device->Bar[Bir] + Off;
        /* 真机：表在 BAR 高偏移；UEFI 可能留下未 mask 表项 */
        MapMmioPages(Table, (UINTN)TableSize * 16u + 16u);
        for (i = 0; i < TableSize; i++) {
            *(volatile UINT32 *)(UINTN)(Table + (UINT64)i * 16u + 12u) = 1;
        }

        MsgAddr = 0xFEE00000u | ((UINT32)DestApic << 12);
        *(volatile UINT32 *)(UINTN)(Table + 0) = MsgAddr;
        *(volatile UINT32 *)(UINTN)(Table + 4) = 0;
        *(volatile UINT32 *)(UINTN)(Table + 8) = Vector;
        __asm__ volatile ("mfence" ::: "memory");

        Dw0 |= (1u << 31);   /* Enable */
        Dw0 &= ~(1u << 30);  /* Clear Function Mask */
        PciWriteConfig(Device->Bus, Device->Device, Device->Function, (UINT8)Cap, Dw0);

        *(volatile UINT32 *)(UINTN)(Table + 12) = 0; /* unmask entry 0 */
        __asm__ volatile ("mfence" ::: "memory");

        DebugWrite("MSI-X enabled vec=");
        DebugHex32(Vector);
        DebugWrite(" dest=");
        DebugHex32(DestApic);
        DebugWrite("\n");
        return 1;
        }
    }

    Cap = PciFindCap(Device->Bus, Device->Device, Device->Function, 0x05);
    if (!Cap) {
        DebugWrite("PCI: no MSI/MSI-X\n");
        return 0;
    }
    {
        UINT32 Dw0 = PciReadConfig(Device->Bus, Device->Device, Device->Function, (UINT8)Cap);
        UINT16 Ctl = (UINT16)(Dw0 >> 16);
        UINT32 MsgAddr = 0xFEE00000u | ((UINT32)DestApic << 12);
        if (Ctl & (1u << 7)) {
            PciWriteConfig(Device->Bus, Device->Device, Device->Function, (UINT8)(Cap + 4), MsgAddr);
            PciWriteConfig(Device->Bus, Device->Device, Device->Function, (UINT8)(Cap + 8), 0);
            {
                UINT32 DataDw = PciReadConfig(Device->Bus, Device->Device, Device->Function, (UINT8)(Cap + 12));
                PciWriteConfig(Device->Bus, Device->Device, Device->Function, (UINT8)(Cap + 12),
                               (DataDw & 0xFFFF0000u) | Vector);
            }
        } else {
            PciWriteConfig(Device->Bus, Device->Device, Device->Function, (UINT8)(Cap + 4), MsgAddr);
            {
                UINT32 DataDw = PciReadConfig(Device->Bus, Device->Device, Device->Function, (UINT8)(Cap + 8));
                PciWriteConfig(Device->Bus, Device->Device, Device->Function, (UINT8)(Cap + 8),
                               (DataDw & 0xFFFF0000u) | Vector);
            }
        }
        Ctl |= 1;
        Dw0 = PciReadConfig(Device->Bus, Device->Device, Device->Function, (UINT8)Cap);
        PciWriteConfig(Device->Bus, Device->Device, Device->Function, (UINT8)Cap,
                       (Dw0 & 0xFFFFu) | ((UINT32)Ctl << 16));
        DebugWrite("MSI enabled vec=");
        DebugHex32(Vector);
        DebugWrite("\n");
        return 1;
    }
}

int PciEnableIoApicIntx(USB_CONTROLLER *Device, UINT8 Vector, UINT8 DestApicId) {
    if (!Device || !IoApicReady()) {
        return 0;
    }
    if (IoApicRoutePciIntx(Device, Vector, DestApicId) != 0) {
        return 0;
    }
    DebugWrite("IOAPIC INTx enabled vec=");
    DebugHex32(Vector);
    DebugWrite(" dest=");
    DebugHex32(DestApicId);
    DebugWrite("\n");
    return 1;
}