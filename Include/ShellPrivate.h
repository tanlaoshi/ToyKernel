/*
 * ShellPrivate.h — Shell 命令分文件内部（仅 Common/Services/ShellCommands；User 勿 include）
 *
 * PR-S-shell-split：分文件命令注册入口。
 */
#ifndef SHELL_PRIVATE_H
#define SHELL_PRIVATE_H

void ShellCommandsUsbRegister(void);
void ShellCommandsNetRegister(void);
void ShellCommandsNetAddrRegister(void);
void ShellCommandsSystemRegister(void);
void ShellCommandsSystemRegisterVirtMin(void);
void ShellCommandsFsUiRegister(void);
void ShellCommandsInstallRegister(void);

#endif
