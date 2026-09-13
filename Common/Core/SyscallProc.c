/*
 * SyscallProc.c — PR-S-syscall-split-1：execve / 用户窗 GUI 系统调用
 */
#include "SyscallPriv.h"
#include "Scheduler.h"
#include "VirtualMemory.h"
#include "Process.h"
#include "CoreOps.h"

int SysExecve(HAL_INTERRUPT_FRAME *Frame, UINT64 UserPath, UINT64 UserArgv,
                     UINT64 UserEnvp) {
    char Path[PATH_MAX_LEN + 1];
    UINTN i;
    TASK *T = SchedulerCurrent();

    if (!T || !T->IsUser || !Frame) {
        return -1;
    }
    VirtualMemoryLoadPageTable(T->PageRoot);
    for (i = 0; i < PATH_MAX_LEN; i++) {
        char C;
        if (VirtualMemoryCopyFromUser(&C, UserPath + i, 1) < 0) {
            VirtualMemoryLoadPageTable(VirtualMemoryKernelRoot());
            return -1;
        }
        Path[i] = C;
        if (C == 0) {
            break;
        }
    }
    Path[PATH_MAX_LEN] = 0;
    VirtualMemoryLoadPageTable(VirtualMemoryKernelRoot());
    if (Path[0] == 0) {
        return -1;
    }
    return ProcessExecve(Frame, Path, UserArgv, UserEnvp);
}

int SysCreateWindow(UINT64 UserTitle, UINT32 W, UINT32 H) {
    char Title[64];
    TASK *T = SchedulerCurrent();

    if (!T || !T->IsUser || W == 0 || H == 0) {
        return -1;
    }
    if (CopyUserCString(Title, UserTitle, sizeof(Title)) < 0 || Title[0] == 0) {
        return -1;
    }
    return WindowOpenUser(Title, W, H);
}

int SysDamage(int Wid, UINT64 UserText) {
    char Text[WIN_STR_MAX + 1];
    TASK *T = SchedulerCurrent();

    if (!T || !T->IsUser || Wid < 0) {
        return -1;
    }
    if (CopyUserCString(Text, UserText, sizeof(Text)) < 0) {
        return -1;
    }
    return WindowDamageUser(Wid, Text);
}

int SysPollInput(int Wid) {
    TASK *T = SchedulerCurrent();

    if (!T || !T->IsUser) {
        return -1;
    }
    return WindowPollUserInput(Wid);
}

int SysUiButton(int Wid, int ButtonId, UINT64 UserLabel) {
    char Label[24];
    TASK *T = SchedulerCurrent();

    if (!T || !T->IsUser) {
        return -1;
    }
    if (CopyUserCString(Label, UserLabel, sizeof(Label)) < 0 || Label[0] == 0) {
        return -1;
    }
    return WindowAddButton(Wid, ButtonId, Label);
}

