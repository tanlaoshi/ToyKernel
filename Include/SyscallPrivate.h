/*
 * SyscallPrivate.h — PR-S-syscall-split-1：syscall 内部（仅 Core；User 勿 include）
 */
#ifndef SYSCALL_PRIVATE_H
#define SYSCALL_PRIVATE_H

#include "Syscall.h"
#include "Hal.h"
#include "Scheduler.h"

#define COPY_BUF_MAX 256
/* 与 TASK_FD.Path[64] 对齐，便于 CRT 打开子路径 */
#define PATH_MAX_LEN 63
#define WIN_STR_MAX  127

int CopyUserCString(char *Dst, UINT64 UserSrc, UINTN MaxLen);

/* SyscallFs.c / SyscallFsSocket.c */
int SysWrite(int Fd, UINT64 UserBuf, UINTN Len);
int SysOpen(UINT64 UserPath);
int SysRead(int Fd, UINT64 UserBuf, UINTN Len);
int SysClose(int Fd);
INT64 SysLseek(int Fd, INT64 Offset, int Whence);
int SysFileStat(UINT64 UserPath, UINT64 UserOut);
int SysFstat(int Fd, UINT64 UserOut);
int SysOpenDirectory(UINT64 UserPath);
int SysReadDirectory(int Fd, UINT64 UserOut);
int SysSocket(int Domain, int Type, UINT64 Protocol);
int SysConnect(int Fd, UINT32 Ip, UINT16 Port);
int SysBind(int Fd, UINT32 Ip, UINT16 Port);
int SysListen(int Fd, int Backlog);
int SysAccept(int Fd);
int SysPipe(UINT64 UserPtr);
int SysDup(int Fd);
/* 相对路径拼到 TASK.Cwd；绝对路径（/ 或 卷:）原样 */
int CwdResolve(TASK *T, char *Path, int Max);
int SysGetcwd(UINT64 UserBuf, UINTN Len);
int SysChdir(UINT64 UserPath);
int SysGetPid(void);
int SysGetPpid(void);

/* SyscallProc.c */
int SysExecve(HAL_INTERRUPT_FRAME *Frame, UINT64 UserPath, UINT64 UserArgv,
              UINT64 UserEnvp);
int SysCreateWindow(UINT64 UserTitle, UINT32 W, UINT32 H);
int SysDamage(int Wid, UINT64 UserText);
int SysDamageRect(int Wid, UINT64 UserDesc);
int SysPollInput(int Wid);
int SysUiButton(int Wid, int ButtonId, UINT64 UserLabel);

#endif
