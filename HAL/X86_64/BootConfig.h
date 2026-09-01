/*
 * BootConfig.h — x86 UEFI 引导参数（仅 ToyBoot ↔ HAL/X86_64/Startup 使用）
 *
 * 布局必须与 ToyBoot/Boot.c 中对应结构完全一致。
 * 不要将此头文件包含进 EDK2 工程。
 */
#ifndef BOOT_CONFIG_H
#define BOOT_CONFIG_H

#include "BootTypes.h"
#include "BootInfo.h"

typedef UINT64 EFI_PHYSICAL_ADDRESS;
typedef void   VOID;

typedef struct {
    VOID  *Buffer;
    UINTN  MapSize;
    UINTN  MapKey;
    UINTN  DescriptorSize;
    UINT32 DescriptorVersion;
} MEMORY_MAP;

typedef struct {
    VIDEO_CONFIG         VideoConfig;
    MEMORY_MAP           MemoryMap;
    EFI_PHYSICAL_ADDRESS KernelEntry;
    EFI_PHYSICAL_ADDRESS RsdpAddress;
    VOID                *SystemTable;
    UINT64               XhciBaseAddress;
} BOOT_CONFIG;

#endif
