/*
 * Block.h — 块设备读写抽象（与文件系统解耦）
 *
 * 后端当前为 ATA；日后可挂 NVMe/AHCI，Fat/Gpt 无需改动。
 */
#ifndef BLOCK_H
#define BLOCK_H

#include "BootConfig.h"

#define BLOCK_SECTOR_SIZE 512
#define BLOCK_MAX_DRIVES  2

int BlockSelect(UINT32 Drive);
UINT32 BlockCurrentDrive(void);
int BlockInit(void);
int BlockReadSectors(UINT32 Lba, UINT32 Count, void *Buffer);
int BlockWriteSectors(UINT32 Lba, UINT32 Count, const void *Buffer);

#endif
