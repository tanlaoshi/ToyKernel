/*
 * ShellPrivate.h — Shell 命令分文件内部（仅 Common/Services/ShellCommands；User 勿 include）
 *
 * PR-S-shell-split：分文件命令注册入口。
 */
#ifndef SHELL_PRIVATE_H
#define SHELL_PRIVATE_H

#include "StoreJob.h"

void ShellCommandsUsbRegister(void);
void ShellCommandsNetRegister(void);
void ShellCommandsNetAddrRegister(void);
void ShellCommandsNetUdpRegister(void);
void ShellCommandsNetTcpRegister(void);
void ShellCommandsNetLwipRegister(void);
void ShellCommandsSystemRegister(void);
void ShellCommandsSystemRegisterVirtMin(void);
void ShellCommandsThemeRegister(void);
void ShellCommandsFsUiRegister(void);
void ShellCommandsFsUiStoreRegister(void);
void ShellCommandsFsExtraRegister(void);
void ShellFatReport(const char *Cmd, int Err);
/* FsUi store 内部分文件共用（仅 ShellCommands） */
int ShellStoreJob(STORE_JOB_KIND Kind, const char *Id);
void ShellStoreQueued(const char *Verb, const char *Id);
void ShellStoreNetDone(const char *Verb, int Err);
void ShellStoreJobStatus(void);
int StoreWordEq(const char *A, const char *B);
void StorePrintUsage(void);
void StoreCmdListCatalog(void);
void ShellCommandsInstallRegister(void);
void ShellCommandsAudioRegister(void);
void ShellCommandsThreadRegister(void);

#endif
