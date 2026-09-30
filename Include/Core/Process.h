#ifndef PROCESS_H
#define PROCESS_H

#include "BootTypes.h"
#include "Hal.h"

int ProcessExec(const char *Path);
/* PR-P1：替换当前用户映像；成功 0（Frame 已改写），失败 -1 */
int ProcessExecve(HAL_INTERRUPT_FRAME *Frame, const char *Path, UINT64 UserArgv,
                  UINT64 UserEnvp);
/* PR-P3：rdi=new_brk（0=查询）；成功返回 break，失败 (UINT64)-1 */
UINT64 ProcessBrk(UINT64 NewBrk);
/* PR-U-mmap / PR-U-mmap2：rdi=len rsi=prot rdx=flags；成功返回 VA，失败 (UINT64)-1
 * 匿名：MAP_ANONYMOUS。文件：MAP_PRIVATE，flags 高 16 位为 fd，offset 固定 0。 */
UINT64 ProcessMmap(UINT64 Len, UINT64 Prot, UINT64 Flags);
UINT64 ProcessMunmap(UINT64 Addr, UINT64 Len);
int ProcessRunDemo(void);

#endif
