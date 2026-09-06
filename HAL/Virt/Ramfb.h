/*
 * Ramfb.h — QEMU ramfb via fw_cfg（PR-V2；HAL 内，非 GOP/UEFI）
 */
#ifndef HAL_RAMFB_H
#define HAL_RAMFB_H

#include "BootInfo.h"

/* 默认 virt 分辨率（XR24 / XRGB8888） */
#define RAMFB_WIDTH  800u
#define RAMFB_HEIGHT 600u

/*
 * 在 *FreeStart 处切出帧缓冲，经 fw_cfg 写 etc/ramfb，填 Info 的 FB 字段，
 * 并推进 *FreeStart。成功 0；无 fw_cfg/ramfb 或内存不足则 -1（FB 保持 0）。
 */
int RamfbSetup(BOOT_INFO *Info, UINT64 FwCfgBase, UINT64 *FreeStart, UINT64 RamEnd);

#endif
