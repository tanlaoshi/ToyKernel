/*
 * Ata.h — ATA PIO 磁盘驱动接口
 *
 * 访问主通道主盘（Primary Master，端口 0x1F0），用于读取 QEMU fat: 虚拟磁盘。
 */
#ifndef ATA_H
#define ATA_H

#include "BootConfig.h"

int AtaInit(void);
int AtaReadSectors(UINT32 Lba, UINT32 Count, void *Buffer);

#endif
