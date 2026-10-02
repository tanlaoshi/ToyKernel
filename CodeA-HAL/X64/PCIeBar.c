/*
 * PCIeBar.c — PCI BAR 大小探测（PR-DEV-bar-size · 阶段 4 第 1 刀）
 *
 * 标准方法：向 BAR 槽写 0xFFFFFFFF，读回掩码算 size，立即写回原值。
 * 只短暂写 BAR 槽（0x10+），绝不写 Command（0x04）。
 * 64-bit MMIO BAR：内部同时探测高 dword 并合成 64 位 size；
 *   高 dword 槽本身由调用方置 0（避免双重登记）。
 *
 * 约束：新 .c ≤300；不写 Command；仅 x86（HAL/X64/ 通配编译，arm64/riscv 不引用）。
 */
#include "PCIe.h"
#include "Debug.h"

#define PCI_BAR_BASE 0x10

/* 由 32 位掩码算 size：清可写位取反加一。IsIo=1 用 IO 掩码（低 2 位清），
 * 否则用 MMIO32 掩码（低 4 位清）。掩码全 0 视为未实现，返回 0。 */
static UINT64 MaskToSize32(UINT32 Mask, int IsIo) {
    UINT32 M;

    M = IsIo ? (Mask & 0xFFFFFFFCu) : (Mask & 0xFFFFFFF0u);
    if (M == 0) {
        return 0;
    }
    /* ~M 为 UINT32（不提升到 64 位），避免高位填 0xFFFFFFFF 产生假冲突 */
    return (UINT64)((~M) + 1u);
}

UINT64 PciBarSize(UINT8 Bus, UINT8 Dev, UINT8 Func, int Bar) {
    UINT8 Off;
    UINT32 Orig;
    UINT32 Mask;
    int IsIo;
    int Is64;

    if (Bar < 0 || Bar > 5) {
        return 0;
    }
    Off = (UINT8)(PCI_BAR_BASE + Bar * 4);
    Orig = PciReadConfig(Bus, Dev, Func, Off);
    if (Orig == 0 || Orig == 0xFFFFFFFFu) {
        return 0;
    }
    IsIo = (Orig & 1u) ? 1 : 0;
    Is64 = (!IsIo && (Orig & 6u) == 4 && Bar + 1 <= 5) ? 1 : 0;

    /* 探测低 dword：写全 1、读掩码、写回原值 */
    PciWriteConfig(Bus, Dev, Func, Off, 0xFFFFFFFFu);
    Mask = PciReadConfig(Bus, Dev, Func, Off);
    PciWriteConfig(Bus, Dev, Func, Off, Orig);

    if (IsIo) {
        return MaskToSize32(Mask, 1);
    }
    if (!Is64) {
        return MaskToSize32(Mask, 0);
    }

    /* 64-bit MMIO：再探测高 dword，合成 64 位 size */
    {
        UINT8 HighOff = (UINT8)(PCI_BAR_BASE + (Bar + 1) * 4);
        UINT32 OrigHi = PciReadConfig(Bus, Dev, Func, HighOff);
        UINT32 Hi;
        UINT64 Combined;
        UINT64 M;

        PciWriteConfig(Bus, Dev, Func, HighOff, 0xFFFFFFFFu);
        Hi = PciReadConfig(Bus, Dev, Func, HighOff);
        PciWriteConfig(Bus, Dev, Func, HighOff, OrigHi);

        /* 高 dword 不可写（Hi=0）= 32-bit addressable：按低 dword 算 size，
         * 避免 64 位 ~M+1 把高位填 0xFFFFFFFF 产生假冲突。 */
        if (Hi == 0) {
            return MaskToSize32(Mask, 0);
        }
        Combined = ((UINT64)Hi << 32) | (UINT64)(Mask & 0xFFFFFFF0u);
        M = Combined & 0xFFFFFFFFFFFFFFF0ULL;
        if (M == 0) {
            return 0;
        }
        return (~M) + 1;
    }
}

/* PR-DEV-mmio-conflict：读 BAR 原值 bit0 判 IO(1)/MMIO(0)；只读不写。 */
int PciBarIsIo(UINT8 Bus, UINT8 Dev, UINT8 Func, int Bar) {
    UINT8 Off;
    UINT32 Raw;

    if (Bar < 0 || Bar > 5) {
        return 0;
    }
    Off = (UINT8)(PCI_BAR_BASE + Bar * 4);
    Raw = PciReadConfig(Bus, Dev, Func, Off);
    return (Raw & 1u) ? 1 : 0;
}
