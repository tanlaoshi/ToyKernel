/*
 * Block.h — 块设备读写抽象（与文件系统解耦）
 *
 * 后端经 BlockRegisterBackend 注册；x86 由 HAL 注册 ATA 实现。
 */
#ifndef BLOCK_H
#define BLOCK_H

#include "BootTypes.h"

#define BLOCK_SECTOR_SIZE 512
#define BLOCK_MAX_DRIVES  2

typedef struct {
    int (*Probe)(UINT32 Drive);
    int (*ReadSectors)(UINT32 Drive, UINT32 Lba, UINT32 Count, void *Buffer);
    int (*WriteSectors)(UINT32 Drive, UINT32 Lba, UINT32 Count, const void *Buffer);
} BLOCK_BACKEND;

void BlockRegisterBackend(const BLOCK_BACKEND *Backend);

int BlockSelect(UINT32 Drive);
UINT32 BlockCurrentDrive(void);
int BlockInit(void);
int BlockReadSectors(UINT32 Lba, UINT32 Count, void *Buffer);
int BlockWriteSectors(UINT32 Lba, UINT32 Count, const void *Buffer);

#endif
