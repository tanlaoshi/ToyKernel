/*
 * Dtb.h — 最小 FDT 解析（HAL 内；Common 不碰 DTB，PR-V1）
 */
#ifndef HAL_DTB_H
#define HAL_DTB_H

#include "BootTypes.h"

/* 从 FDT 取第一块 memory 的 reg；成功 0，失败 -1 */
int DtbMemoryRegion(UINT64 DtbPhys, UINT64 *OutBase, UINT64 *OutSize);

/* 找 compatible=qemu,fw-cfg-mmio 的 reg 基址；成功 0，失败 -1 */
int DtbFwCfgBase(UINT64 DtbPhys, UINT64 *OutBase);

#endif
