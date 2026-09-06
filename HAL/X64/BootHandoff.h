/*
 * BootHandoff.h - ToyBoot <-> Kernel handoff ABI (PR-R1)
 *
 * Single layout source for EDK2 and freestanding kernel.
 * Keep out of ToyKernel/Include (Common stays UEFI-free).
 *
 * x86_64 sizes: VIDEO 32, MEMORY_MAP 40, BOOT_CONFIG 104.
 */
#ifndef TOY_BOOT_HANDOFF_H
#define TOY_BOOT_HANDOFF_H

#ifndef EFIAPI
#include "BootTypes.h"
typedef void VOID;
#endif

typedef struct {
    UINT64 FrameBufferBase;
    UINT64 FrameBufferSize;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    UINT32 PixelsPerScanLine;
} TOY_VIDEO_CONFIG;

typedef struct {
    VOID  *Buffer;
    UINTN  MapSize;
    UINTN  MapKey;
    UINTN  DescriptorSize;
    UINT32 DescriptorVersion;
} TOY_MEMORY_MAP;

typedef struct {
    TOY_VIDEO_CONFIG     VideoConfig;
    TOY_MEMORY_MAP       MemoryMap;
    UINT64               KernelEntry;
    UINT64               RsdpAddress;
    VOID                *SystemTable;
    UINT64               XhciBaseAddress;
} TOY_BOOT_CONFIG;

#if defined(__GNUC__)
_Static_assert(sizeof(TOY_VIDEO_CONFIG) == 32, "TOY_VIDEO_CONFIG size");
_Static_assert(sizeof(TOY_MEMORY_MAP) == 40, "TOY_MEMORY_MAP size");
_Static_assert(sizeof(TOY_BOOT_CONFIG) == 104, "TOY_BOOT_CONFIG size");
_Static_assert(__builtin_offsetof(TOY_BOOT_CONFIG, VideoConfig) == 0, "VideoConfig off");
_Static_assert(__builtin_offsetof(TOY_BOOT_CONFIG, MemoryMap) == 32, "MemoryMap off");
_Static_assert(__builtin_offsetof(TOY_BOOT_CONFIG, KernelEntry) == 72, "KernelEntry off");
_Static_assert(__builtin_offsetof(TOY_BOOT_CONFIG, RsdpAddress) == 80, "RsdpAddress off");
_Static_assert(__builtin_offsetof(TOY_BOOT_CONFIG, SystemTable) == 88, "SystemTable off");
_Static_assert(__builtin_offsetof(TOY_BOOT_CONFIG, XhciBaseAddress) == 96, "XhciBase off");
#endif

#endif
