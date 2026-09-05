/*
 * ShellCommands.h — Shell 扩展命令注册
 */
#ifndef SHELL_COMMANDS_H
#define SHELL_COMMANDS_H

void ShellCommandsRegister(void);
/* PR-A9：virt 串口最小集 help(已内置)/mem/ps/halt */
void ShellCommandsRegisterVirtMin(void);
/* PR-R3：ls/cat/write/wrbig/rm/mkdir/rmdir/mv/vols */
void ShellCommandsRegisterFs(void);
void ShellOnInterrupt(void);

#endif
