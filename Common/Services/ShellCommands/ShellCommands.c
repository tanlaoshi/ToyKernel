/*
 * ShellCommands.c — PR-S-shell-split-3：注册汇总（命令体见 ShellCommands*.c）
 */
#include "ShellCommands.h"
#include "ShellPrivate.h"

void ShellCommandsRegisterVirtMin(void) {
    ShellCommandsSystemRegisterVirtMin();
}

void ShellCommandsRegister(void) {
    /* 目录类二级在 ShellCommandsRegisterFs（FileSystem.c） */
    ShellCommandsSystemRegister();
    ShellCommandsUsbRegister();
    ShellCommandsNetRegister();
    ShellCommandsFsUiRegister();
}
