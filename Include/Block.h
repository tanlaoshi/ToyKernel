/*
 * Block.h — 块设备读写抽象（与文件系统解耦）
 *
 * 后端经 BlockRegisterBackend / ToyDrvBlockAttach（PR-D2）注册；
 * x86 ATA、virt Arm/RiscV virtio-blk 经 Drv Block 类挂上。
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
    /* 可选：把写缓冲刷到介质；NULL → BlockFlush 成功空操作（PR-F2） */
    int (*Flush)(UINT32 Drive);
} BLOCK_BACKEND;

void BlockRegisterBackend(const BLOCK_BACKEND *Backend);

int BlockSelect(UINT32 Drive);
UINT32 BlockCurrentDrive(void);
int BlockInit(void);
/* 是否已挂后端（PR-D2：Drv Bind 之后为真） */
int BlockBackendReady(void);
int BlockReadSectors(UINT32 Lba, UINT32 Count, void *Buffer);
int BlockWriteSectors(UINT32 Lba, UINT32 Count, const void *Buffer);
/* PR-F2：落盘；无 Flush 回调时返回 1（成功） */
int BlockFlush(void);

#endif
