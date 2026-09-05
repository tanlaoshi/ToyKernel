/*
 * BootConfig.h — x86 UEFI 引导参数（ToyBoot ↔ HAL/X86_64/Startup）
 *
 * 布局见 BootHandoff.h（单一 ABI 源，PR-R1）。
 */
#ifndef BOOT_CONFIG_H
#define BOOT_CONFIG_H

#include "BootHandoff.h"
#include "BootInfo.h"

typedef TOY_BOOT_CONFIG BOOT_CONFIG;
typedef TOY_MEMORY_MAP  MEMORY_MAP;

#if defined(__GNUC__)
/* Common VIDEO_CONFIG 与交接 TOY_VIDEO_CONFIG 同布局 */
_Static_assert(sizeof(VIDEO_CONFIG) == sizeof(TOY_VIDEO_CONFIG), "VIDEO_CONFIG ABI");
#endif

#endif
