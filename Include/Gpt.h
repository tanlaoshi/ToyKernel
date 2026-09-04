/*
 * Gpt.h — 分区表解析接口
 *
 * 支持 superfloppy（LBA0 即 FAT）、MBR 分区、GPT 分区，定位 FAT 卷起始 LBA。
 */
#ifndef GPT_H
#define GPT_H

#include "BootTypes.h"

/* 找当前 Block 盘上的 FAT 起始 LBA；成功非 0 */
int GptFindFatStart(UINT32 *OutLba);

/*
 * PR-FS2：同上，并报告是否为 GPT EFI System Partition（可作只读 ESP）。
 * OutIsEsp 可为 NULL；MBR/superfloppy 时 *OutIsEsp=0。
 */
int GptFindFatStartEx(UINT32 *OutLba, int *OutIsEsp);

#endif
