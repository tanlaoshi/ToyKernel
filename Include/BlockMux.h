/*
 * BlockMux.h — 主存储 + USB MSC 共用 BLOCK_BACKEND（PR-H-msc）
 *
 * Drive0 = Primary（NVMe/AHCI/ATA）若有，否则 MSC
 * Drive1 = MSC（仅当 Primary 已占 Drive0）
 *
 * PR-H-msc-1：仅头文件 + 实现就位；MSC Probe 永不就绪，不改变现有盘枚举。
 */
#ifndef BLOCK_MUX_H
#define BLOCK_MUX_H

#include "Block.h"

/* 安装/更新 MSC 后端：保留当前 Primary，注册 Mux */
void BlockMuxInstallMsc(const BLOCK_BACKEND *Msc);

#endif
