/*
 * ShellPrivate.h — Shell 分文件内部（仅 Common/Services；User 勿 include）
 *
 * PR-S-shell-split：分文件命令注册入口。
 */
#ifndef SHELL_PRIVATE_H
#define SHELL_PRIVATE_H

void ShellCmdUsbRegister(void);
void ShellCmdNetRegister(void);
void ShellCmdNetAddrRegister(void);
void ShellCmdSystemRegister(void);
void ShellCmdSystemRegisterVirtMin(void);
void ShellCmdFsUiRegister(void);
void ShellCmdInstallRegister(void);

#endif
