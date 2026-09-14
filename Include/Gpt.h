/*
 * Gpt.h — 分区表解析接口
 *
 * 支持 superfloppy（LBA0 即 FAT）、MBR 分区、GPT 分区，定位 FAT 卷起始 LBA。
 */
#ifndef GPT_H
#define GPT_H

#include "BootTypes.h"

#define GPT_MAX_FAT_PARTS 8

typedef struct {
    UINT32 StartLba;
    int    IsEsp; /* GPT EFI System Partition */
} GPT_FAT_PART;

/* 找当前 Block 盘上的 FAT 起始 LBA；成功非 0 */
int GptFindFatStart(UINT32 *OutLba);

/*
 * PR-FS2：同上，并报告是否为 GPT EFI System Partition（可作只读 ESP）。
 * OutIsEsp 可为 NULL；MBR/superfloppy 时 *OutIsEsp=0。
 */
int GptFindFatStartEx(UINT32 *OutLba, int *OutIsEsp);

/*
 * 枚举当前盘上全部 FAT 分区（ESP + Basic Data + 引导扇区像 FAT 的项）。
 * 成功返回个数；0=无。Out 可写最多 Max 项。
 */
int GptFindAllFat(GPT_FAT_PART *Out, int Max);

/*
 * PR-FS-inst-1：在当前 Block 盘写入 protective MBR + GPT（ESP + TOYOS）。
 * EspMib：ESP 大小（MiB），建议 256；TotalSectors：整盘扇区数。
 * 成功非 0，并填分区起始/扇区数。
 */
int GptWriteToyLayout(UINT64 TotalSectors, UINT32 EspMib,
                      UINT32 *OutEspLba, UINT32 *OutEspSectors,
                      UINT32 *OutToyLba, UINT32 *OutToySectors);

#endif
