/*
 * XhciMmio.c — PR-H-xhci-core-split-1：MMIO / 内存 / Stall / Wait / MapDma
 *
 * 从 Xhci.c 原样搬家；不改语义。全局仍定义在 Xhci.c。
 */
#include "XHCI/XhciInternal.h"

/* 读 MMIO 32 位 */
UINT32 ReadMmio32(UINT64 Addr) {
    return *(volatile UINT32 *)(UINTN)Addr;
}

/* 写 MMIO 32 位 */
void WriteMmio32(UINT64 Addr, UINT32 Value) {
    *(volatile UINT32 *)(UINTN)Addr = Value;
}

/* 写 MMIO 64 位（分两次 32 位写） */
void WriteMmio64(UINT64 Addr, UINT64 Value) {
    WriteMmio32(Addr, (UINT32)Value);
    WriteMmio32(Addr + 4, (UINT32)(Value >> 32));
}

UINT64 ReadMmio64(UINT64 Addr) {
    UINT64 Lo = ReadMmio32(Addr);
    UINT64 Hi = ReadMmio32(Addr + 4);
    return Lo | (Hi << 32);
}

/* 虚拟地址转物理地址（恒等映射） */
UINT64 PointerToPhysical(const void *Ptr) {
    return (UINT64)(UINTN)Ptr;
}

/* 内存屏障，保证 TRB 写入对硬件可见 */
void Fence(void) {
    __asm__ volatile ("mfence" ::: "memory");
}

/* 把 DMA 缓冲从 CPU cache 推出去（真机 RS 后 DMA 读环/DCBAA） */
void FlushDma(const void *Ptr, UINTN Size) {
    const UINT8 *P = (const UINT8 *)Ptr;
    UINTN Off;

    if (!Ptr || Size == 0) {
        return;
    }
    for (Off = 0; Off < Size; Off += 64) {
        __asm__ volatile("clflush (%0)" : : "r"(P + Off) : "memory");
    }
    Fence();
}

/* 清零内存块 */
void ZeroMemory(void *Ptr, UINTN Size) {
    UINT8 *P = (UINT8 *)Ptr;
    while (Size--) {
        *P++ = 0;
    }
}

void CopyMemory(void *Dst, const void *Src, UINTN Size) {
    UINT8 *D = (UINT8 *)Dst;
    const UINT8 *S = (const UINT8 *)Src;
    while (Size--) {
        *D++ = *S++;
    }
}


/* 等待寄存器 Mask 位清零 */
int WaitClear(UINT64 Addr, UINT32 Mask, int Timeout) {
    while (Timeout--) {
        if (!(ReadMmio32(Addr) & Mask)) {
            return 1;
        }
    }
    return 0;
}

/* 等待寄存器 Mask 位置位 */
int WaitSet(UINT64 Addr, UINT32 Mask, int Timeout) {
    while (Timeout--) {
        if (ReadMmio32(Addr) & Mask) {
            return 1;
        }
    }
    return 0;
}

UINT64 ReadTsc(void) {
    UINT32 Lo;
    UINT32 Hi;

    __asm__ volatile ("rdtsc" : "=a"(Lo), "=d"(Hi));
    return ((UINT64)Hi << 32) | Lo;
}

/* 真机忙等，按 ~3GHz 估算。QEMU 不要用长 Stall。
 * 每 ~1ms Drain 一次事件环，避免 Reset/claim 长 Stall 把 HID 饿死。 */
void StallMs(UINT32 Ms) {
    UINT64 T0;
    UINT64 Need;
    UINT64 NextDrain;

    if (Ms == 0) {
        return;
    }
    Need = (UINT64)Ms * 3000000ULL;
    T0 = ReadTsc();
    NextDrain = T0;
    while (ReadTsc() - T0 < Need) {
        if (!HalCpuIsHypervisor() && gXhciStarted && ReadTsc() >= NextDrain &&
            !XhciEventIsExclusive() && !gXhciCmdWaiting) {
            ProcessEventsRealPc();
            ServiceHidCompletions();
            NextDrain = ReadTsc() + 3000000ULL;
        }
        __asm__ volatile ("pause");
    }
}

int WaitSetMs(UINT64 Addr, UINT32 Mask, UINT32 Ms) {
    UINT64 T0;
    UINT64 Need;

    Need = (UINT64)Ms * 3000000ULL;
    T0 = ReadTsc();
    for (;;) {
        if (ReadMmio32(Addr) & Mask) {
            return 1;
        }
        if (ReadTsc() - T0 >= Need) {
            return 0;
        }
        __asm__ volatile ("pause");
    }
}

int WaitClearMs(UINT64 Addr, UINT32 Mask, UINT32 Ms) {
    UINT64 T0;
    UINT64 Need;

    Need = (UINT64)Ms * 3000000ULL;
    T0 = ReadTsc();
    for (;;) {
        if (!(ReadMmio32(Addr) & Mask)) {
            return 1;
        }
        if (ReadTsc() - T0 >= Need) {
            return 0;
        }
        __asm__ volatile ("pause");
    }
}

int MapXhciDma(UINT64 Phys, UINTN Bytes) {
    UINT64 Page = Phys & ~0xFFFULL;
    UINTN Span = (UINTN)((Phys + Bytes + 0xFFFULL) - Page);
    if (!VirtualMemoryEnabled()) {
        return 0;
    }
    if (VirtualMemoryMapRange(Page, Page, Span, PTE_XHCI_DMA) == 0) {
        return 0;
    }
    /*
     * 低位 identity 常为 2MB huge，PageWalk 无法拆 PTE → Map 失败。
     * 仍可经 huge 访问；仅缺 UC。高位无映射则必须失败。
     */
    if (Page + Span <= (512ULL << 20)) {
        return 0;
    }
    return -1;
}
