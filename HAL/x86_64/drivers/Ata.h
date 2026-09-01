/*
 * Ata.h — ATA PIO 磁盘驱动接口
 *
 * Primary 通道：Drive 0=Master，Drive 1=Slave（端口 0x1F0）。
 */
#ifndef ATA_H
#define ATA_H

#include "BootConfig.h"

int AtaProbe(UINT32 Drive);
int AtaInit(void);
int AtaReadSectors(UINT32 Drive, UINT32 Lba, UINT32 Count, void *Buffer);
int AtaWriteSectors(UINT32 Drive, UINT32 Lba, UINT32 Count, const void *Buffer);

#endif
