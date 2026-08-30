/*
 * BootConfig.h — 引导配置与内核共享数据结构
 *
 * 布局必须与 ToyBoot/Boot.c 中对应结构完全一致（x86-64 自然对齐）。
 * 不要将此头文件包含进 EDK2 工程，UINT64 等类型会与 ProcessorBind.h 冲突。
 */
#ifndef BOOT_CONFIG_H
#define BOOT_CONFIG_H

#define NULL ((void *)0)

typedef unsigned long long  UINT64;
typedef unsigned int        UINT32;
typedef unsigned short      UINT16;
typedef unsigned char       UINT8;
typedef UINT64              UINTN;
typedef UINT64              EFI_PHYSICAL_ADDRESS;
typedef void                VOID;

/* UEFI GOP 视频模式信息（帧缓冲地址、分辨率等） */
typedef struct {
    UINT64 FrameBufferBase;
    UINT64 FrameBufferSize;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    UINT32 PixelsPerScanLine;
} VIDEO_CONFIG;

/* UEFI GetMemoryMap 结果，ExitBootServices 后内核仍可使用 Buffer */
typedef struct {
    VOID   *Buffer;
    UINTN  MapSize;
    UINTN  MapKey;
    UINTN  DescriptorSize;
    UINT32 DescriptorVersion;
} MEMORY_MAP;

/* 引导阶段传给内核的全部启动参数 */
typedef struct {
    VIDEO_CONFIG         VideoConfig;
    MEMORY_MAP           MemoryMap;
    EFI_PHYSICAL_ADDRESS KernelEntry;
    EFI_PHYSICAL_ADDRESS RsdpAddress;
    VOID                *SystemTable;
    UINT64               XhciBaseAddress;
} BOOT_CONFIG;

#endif
