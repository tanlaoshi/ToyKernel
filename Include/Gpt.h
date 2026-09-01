/*
 * Gpt.h — 分区表解析接口
 *
 * 支持 superfloppy（LBA0 即 FAT）、MBR 分区、GPT 分区，定位 FAT 卷起始 LBA。
 */
#ifndef GPT_H
#define GPT_H

#include "BootTypes.h"

int GptFindFatStart(UINT32 *OutLba);

#endif
