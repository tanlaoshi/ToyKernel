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

// 简化的 UEFI 类型
typedef struct {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
} EFI_TABLE_HEADER;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    VOID *FirmwareVendor;
    UINT32 FirmwareRevision;
    VOID *ConIn;
    struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    VOID *StdErr;
    VOID *RuntimeServices;
    VOID *BootServices;
    UINT64 NumberOfTableEntries;
    VOID *ConfigurationTable;
} EFI_SYSTEM_TABLE;

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    VOID *Reset;
    UINT64 (*OutputString)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, const unsigned short *String);
    VOID *TestString;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef struct {
    UINT64 FrameBufferBase;
    UINT64 FrameBufferSize;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    UINT32 PixelsPerScanLine;
} VIDEO_CONFIG;

typedef struct {
    VOID   *Buffer;
    UINTN  MapSize;
    UINTN  MapKey;
    UINTN  DescriptorSize;
    UINT32 DescriptorVersion;
} MEMORY_MAP;

typedef struct {
    VIDEO_CONFIG              VideoConfig;
    MEMORY_MAP                MemoryMap;
    EFI_PHYSICAL_ADDRESS      KernelEntry;
    EFI_PHYSICAL_ADDRESS      RsdpAddress;
    EFI_SYSTEM_TABLE          *SystemTable;
} BOOT_CONFIG;

#endif