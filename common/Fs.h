/*
 * Fs.h — 文件系统模块入口
 *
 * 串联 ATA + GPT + FAT，并向 Console 注册 ls/cat 命令。
 */
#ifndef FS_H
#define FS_H

int FsInit(void);

#endif
