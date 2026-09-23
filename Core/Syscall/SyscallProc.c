/*
 * SyscallProc.c — PR-S-syscall-split-1：execve / 用户窗 GUI 系统调用
 */
#include "SyscallPrivate.h"
#include "Scheduler.h"
#include "VirtualMemory.h"
#include "Process.h"
#include "CoreOps.h"
#include "PhysicalMemory.h"

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
    if (CwdResolve(T, Path, (int)sizeof(Path)) != 0) {
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

/*
 * PR-G-desk-3：用户描述符 {X,Y,W,H,Pixels*}（x86_64 共 24 字节）。
 * 像素上限 GUI_BLIT_MAX_PIXELS，避免大块拷贝撑爆栈。
 */
#define DAMAGE_RECT_DESC_SIZE 24u
#define DAMAGE_RECT_MAX_PX    4096u

int SysDamageRect(int Wid, UINT64 UserDesc) {
    UINT8 Desc[DAMAGE_RECT_DESC_SIZE];
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT64 UserPix;
    UINTN Bytes;
    UINT32 *Buf;
    UINT32 Pages;
    int Rc;
    TASK *T = SchedulerCurrent();

    if (!T || !T->IsUser || Wid < 0 || UserDesc == 0) {
        return -1;
    }
    if (VirtualMemoryCopyFromUser(Desc, UserDesc, DAMAGE_RECT_DESC_SIZE) < 0) {
        return -1;
    }
    X = *(UINT32 *)(Desc + 0);
    Y = *(UINT32 *)(Desc + 4);
    W = *(UINT32 *)(Desc + 8);
    H = *(UINT32 *)(Desc + 12);
    UserPix = *(UINT64 *)(Desc + 16);
    if (W == 0 || H == 0 || UserPix == 0) {
        return -1;
    }
    if ((UINT64)W * (UINT64)H > (UINT64)DAMAGE_RECT_MAX_PX) {
        return -1;
    }
    Bytes = (UINTN)W * (UINTN)H * sizeof(UINT32);
    Pages = (UINT32)((Bytes + 4095u) / 4096u);
    if (Pages == 0) {
        Pages = 1;
    }
    Buf = (UINT32 *)PhysicalMemoryAllocatePages(Pages);
    if (!Buf) {
        return -1;
    }
    if (VirtualMemoryCopyFromUser(Buf, UserPix, Bytes) < 0) {
        PhysicalMemoryFreePages(Buf, Pages);
        return -1;
    }
    Rc = WindowDamageRectUser(Wid, X, Y, W, H, Buf);
    PhysicalMemoryFreePages(Buf, Pages);
    return Rc;
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

