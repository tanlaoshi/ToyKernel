/*
 * Install.h — PR-FS-inst-1：Guest 安装到指定 Block
 */
#ifndef INSTALL_H
#define INSTALL_H

#include "BootTypes.h"

/* 列出已 Probe 的 drive 槽（串口/Console 由调用方打印） */
int InstallListDisks(void (*PrintLine)(UINT32 Drive, int Ready));

/*
 * 将 Drive 做成 GPT + ESP(EspMib) + TOYOS，并写入 BOOTX64 / Kernel / TOYOS.ID / THEME.CFG。
 * TotalSectors：目标盘扇区数（QEMU 冒烟用 --mib 换算）。
 * Yes=0 仅检查；Yes=1 执行。成功 0，失败负值。
 */
int InstallToDrive(UINT32 Drive, UINT64 TotalSectors, UINT32 EspMib, int Yes);

#endif
