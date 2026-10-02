/*
 * Platform.h — x86-64 平台 MMIO / 设备兜底（Common 经 Hal.h 间接使用）
 */
#ifndef PLATFORM_H
#define PLATFORM_H

#include "BootTypes.h"

void HalPlatformMapMmio(void);
void HalPlatformSetXhciFallback(UINT64 Address);
UINT64 HalPlatformXhciFallback(void);
void HalPlatformSetRsdp(UINT64 Address);
UINT64 HalPlatformRsdp(void);
void HalPlatformSetSystemTable(void *SystemTable);
void *HalPlatformSystemTable(void);
/* ExitBootServices 后须映入 EFI Runtime 内存，GetTime 才不会踩缺页 */
void HalPlatformNoteRuntimeRange(UINT64 Phys, UINT64 Size);
int HalPlatformRuntimeRangeCount(void);

#endif
