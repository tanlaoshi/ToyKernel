/*
 * Startup.c — x86-64 UEFI 入口：BOOT_CONFIG → BOOT_INFO → KernelMain
 *
 * 真机注意：UEFI 调用栈常在 >512MB；本内核恒等映射仅低 512MB。
 * 必须在开自有分页前切到 .bss 低地址栈，否则一 mov cr3 就缺页，
 * 屏上仍停在 ToyBoot 最后两行（清屏在 video 模块更后面）。
 */
#include "BootConfig.h"
#include "BootInfo.h"
#include "Kernel.h"
#include "Hal.h"

extern char __kernel_end[];
extern void HalPlatformSetXhciFallback(UINT64 Address);
extern void HalPlatformSetRsdp(UINT64 Address);
extern void HalPlatformSetSystemTable(void *SystemTable);
extern void HalPlatformNoteRuntimeRange(UINT64 Phys, UINT64 Size);

#define EFI_MEMORY_CONVENTIONAL 7
#define EFI_MEMORY_RUNTIME      (1ULL << 63)

typedef struct {
    UINT32 Type;
    UINT32 Pad;
    UINT64 PhysicalStart;
    UINT64 VirtualStart;
    UINT64 NumberOfPages;
    UINT64 Attribute;
} EFI_MEMORY_DESCRIPTOR;

/* 落在 Kernel.elf BSS（Phys ~0x142000），恒等映射范围内 */
static UINT8 gEarlyStack[65536] __attribute__((aligned(16)));

static int BootInfoAddRegion(BOOT_INFO *Info, UINT64 Phys, UINT64 Size, int Free) {
    if (Info->RegionCount >= BOOT_MEMORY_REGIONS_MAX || Size == 0) {
        return -1;
    }
    Info->Regions[Info->RegionCount].Phys = Phys;
    Info->Regions[Info->RegionCount].Size = Size;
    Info->Regions[Info->RegionCount].Free = Free ? 1u : 0u;
    Info->RegionCount++;
    return 0;
}

static void BootInfoFromUefi(BOOT_CONFIG *Cfg, BOOT_INFO *Out, BOOT_CONFIG *CfgPhys) {
    MEMORY_MAP *Map = &Cfg->MemoryMap;
    UINT8 *Base;
    UINTN Count;
    UINTN i;

    Out->FrameBufferBase = Cfg->VideoConfig.FrameBufferBase;
    Out->FrameBufferSize = Cfg->VideoConfig.FrameBufferSize;
    Out->HorizontalResolution = Cfg->VideoConfig.HorizontalResolution;
    Out->VerticalResolution = Cfg->VideoConfig.VerticalResolution;
    Out->PixelsPerScanLine = Cfg->VideoConfig.PixelsPerScanLine;
    Out->RegionCount = 0;
    Out->KernelStart = 0x100000;
    Out->KernelEnd = (UINT64)(UINTN)__kernel_end;

    HalPlatformSetXhciFallback(Cfg->XhciBaseAddress);
    HalPlatformSetRsdp(Cfg->RsdpAddress);
    HalPlatformSetSystemTable(Cfg->SystemTable);

    if (Map->Buffer != 0 && Map->DescriptorSize >= sizeof(EFI_MEMORY_DESCRIPTOR)) {
        Base = (UINT8 *)Map->Buffer;
        Count = Map->MapSize / Map->DescriptorSize;
        for (i = 0; i < Count; i++) {
            EFI_MEMORY_DESCRIPTOR *Desc =
                (EFI_MEMORY_DESCRIPTOR *)(Base + i * Map->DescriptorSize);
            if (Desc->Attribute & EFI_MEMORY_RUNTIME) {
                HalPlatformNoteRuntimeRange(Desc->PhysicalStart,
                                            Desc->NumberOfPages << 12);
            }
            if (Desc->Type != EFI_MEMORY_CONVENTIONAL) {
                continue;
            }
            BootInfoAddRegion(Out, Desc->PhysicalStart,
                              Desc->NumberOfPages << 12, 1);
        }
    }

    BootInfoAddRegion(Out, 0, 4096, 0);
    /* AP trampoline @0x8000、参数/GDT @0x7E00、临时栈 @0x7000 — 勿被 PMM 占用 */
    BootInfoAddRegion(Out, 0x7000, 0x2000, 0);
    BootInfoAddRegion(Out, Out->KernelStart, Out->KernelEnd - Out->KernelStart, 0);
    if (CfgPhys) {
        BootInfoAddRegion(Out, (UINT64)(UINTN)CfgPhys, sizeof(BOOT_CONFIG), 0);
    }
    if (Map->Buffer != 0 && Map->MapSize != 0) {
        BootInfoAddRegion(Out, (UINT64)(UINTN)Map->Buffer, Map->MapSize, 0);
    }
    if (Out->FrameBufferSize != 0) {
        BootInfoAddRegion(Out, Out->FrameBufferBase, Out->FrameBufferSize, 0);
    }
}

/*
 * 在早期栈上继续：此时仍用固件页表，可安全读 BootConfig（可能在高地址）。
 * BootInfo 拷入静态区后，后续开分页不再依赖 UEFI 栈。
 */
__attribute__((noinline, noreturn))
static void KernelEntryContinue(BOOT_CONFIG *BootConfig) {
    static BOOT_INFO Info;
    static BOOT_CONFIG CfgCopy;

    if (BootConfig == 0) {
        for (;;) {
        }
    }

    /* 拷到内核 BSS，避免开分页后仍间接碰 UEFI 栈上的交接块 */
    CfgCopy = *BootConfig;
    BootInfoFromUefi(&CfgCopy, &Info, &CfgCopy);
    BootInfoSet(&Info);
    KernelMain();
    for (;;) {
    }
}

/* ToyBoot 跳转入口（link.ld ENTRY）；SysV=RDI，MS=RCX — 两者都认 */
void KernelEntry(BOOT_CONFIG *BootConfigSysv) {
    BOOT_CONFIG *Cfg;
    UINT64 Stk;

    __asm__ volatile("mov %%rcx, %0" : "=r"(Cfg));
    if (Cfg == 0) {
        Cfg = BootConfigSysv;
    }
    if (Cfg == 0) {
        for (;;) {
        }
    }

    /* 立刻离开可能 >512MB 的 UEFI 栈（仍在固件页表下） */
    Stk = ((UINT64)(UINTN)(gEarlyStack + sizeof(gEarlyStack))) & ~0xFULL;
    __asm__ volatile(
        "mov %[stk], %%rsp\n\t"
        "xor %%rbp, %%rbp"
        :
        : [stk] "r"(Stk)
        : "memory", "cc");
    KernelEntryContinue(Cfg);
}
