#ifndef PROCESS_H
#define PROCESS_H

#include "BootTypes.h"
#include "Hal.h"

int ProcessExec(const char *Path);
/* PR-P1：替换当前用户映像；成功 0（Frame 已改写），失败 -1 */
int ProcessExecve(HAL_FRAME *Frame, const char *Path, UINT64 UserArgv,
                  UINT64 UserEnvp);
int ProcessRunDemo(void);

#endif
