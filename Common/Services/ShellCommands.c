/*
 * ShellCommands.c — PR-S-shell-split-3：注册汇总（命令体见 ShellCmd*.c）
 */
#include "ShellCommands.h"
#include "ShellPrivate.h"

void ShellCommandsRegisterVirtMin(void) {
    ShellCmdSystemRegisterVirtMin();
}

void ShellCommandsRegister(void) {
    /* 目录类二级在 ShellCommandsRegisterFs（FileSystem.c） */
    ShellCmdSystemRegister();
    ShellCmdUsbRegister();
    ShellCmdNetRegister();
    ShellCmdFsUiRegister();
}
