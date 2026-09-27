/*
 * SyscallProc.c — PR-S-syscall-split-1：execve / 用户窗 GUI 系统调用
 */
#include "SyscallPrivate.h"
#include "Scheduler.h"
#include "VirtualMemory.h"
#include "Process.h"
#include "CoreOps.h"
#include "PhysicalMemory.h"
#include "Tasks.h"
#include "Hal.h"

/* 与 User/include/toyos/task.h 同步；改布局须升 VER */
#define TASK_SNAP_MAGIC 0x31535054u
#define TASK_SNAP_VER   1u
#define TASK_SNAP_MAX   16
#define TASK_F_USER     0x1u
#define TASK_F_ZOMBIE   0x2u
#define TASK_F_BLOCKED  0x4u
#define TASK_F_CURRENT  0x8u

typedef struct {
    INT32  Pid;
    INT32  Parent;
    UINT32 Flags;
    INT32  Priority;
    INT32  OnCpu;
    INT32  HomeCpu;
    UINT32 Ticks;
    char   Name[16];
} TASK_SNAP_ENTRY;

typedef struct {
    UINT32          Magic;
    UINT32          Version;
    UINT32          Count;
    UINT32          FreePages;
    UINT64          CpuTicks;
    UINT32          WorkerLoops;
    UINT32          Pad0;
    UINT64          StealCount;
    INT32           SelfPid;
    INT32           Pad1;
    TASK_SNAP_ENTRY Tasks[TASK_SNAP_MAX];
} TASK_SNAP;

_Static_assert(sizeof(TASK_SNAP) <= 1024, "TASK_SNAP too large");

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

/* PR-U-getpid：getpid 返回 TASK.Id；getppid 返回 ParentId（-1 → 0） */
int SysGetPid(void) {
    TASK *T = SchedulerCurrent();

    if (!T || !T->IsUser) {
        return -1;
    }
    return (int)T->Id;
}

int SysGetPpid(void) {
    TASK *T = SchedulerCurrent();

    if (!T || !T->IsUser) {
        return -1;
    }
    return (T->ParentId < 0) ? 0 : T->ParentId;
}

/*
 * SYS_TASK_SNAP：把当前任务表与若干全局计数拷到用户缓冲（同 Shell ps）。
 * Cap 须 >= sizeof(TASK_SNAP)；成功 0，失败 -1。
 */
int SysTaskSnap(UINT64 UserOut, UINTN Cap) {
    TASK_SNAP Snap;
    TASK *Self;
    TASK *Cur;
    int i;
    int N;
    UINTN k;

    Self = SchedulerCurrent();
    if (!Self || !Self->IsUser || UserOut == 0 || Cap < sizeof(Snap)) {
        return -1;
    }

    for (k = 0; k < sizeof(Snap); k++) {
        ((UINT8 *)&Snap)[k] = 0;
    }
    Snap.Magic = TASK_SNAP_MAGIC;
    Snap.Version = TASK_SNAP_VER;
    Snap.FreePages = (UINT32)PhysicalMemoryFreePageCount();
    Snap.CpuTicks = HalCpuTicks(0);
    Snap.WorkerLoops = WorkerLoopCount();
    Snap.StealCount = SchedulerStealCount();
    Snap.SelfPid = (INT32)Self->Id + 1;

    Cur = SchedulerCurrent();
    N = 0;
    for (i = 0; i < MAX_TASKS && N < TASK_SNAP_MAX; i++) {
        const TASK *T = SchedulerTaskByIndex(i);
        TASK_SNAP_ENTRY *E;
        int n;

        if (!T || T->State == TASK_UNUSED) {
            continue;
        }
        E = &Snap.Tasks[N];
        E->Pid = i + 1;
        E->Parent = (T->ParentId < 0) ? 0 : (T->ParentId + 1);
        E->Flags = 0;
        if (T->IsUser) {
            E->Flags |= TASK_F_USER;
        }
        if (T->State == TASK_ZOMBIE) {
            E->Flags |= TASK_F_ZOMBIE;
        } else if (T->State == TASK_BLOCKED) {
            E->Flags |= TASK_F_BLOCKED;
        }
        if (Cur == T) {
            E->Flags |= TASK_F_CURRENT;
        }
        E->Priority = T->Priority;
        E->OnCpu = T->OnCpu;
        E->HomeCpu = T->HomeCpu;
        E->Ticks = T->Ticks;
        for (n = 0; n < 15 && T->Name[n]; n++) {
            E->Name[n] = T->Name[n];
        }
        E->Name[n] = 0;
        N++;
    }
    Snap.Count = (UINT32)N;

    if (VirtualMemoryCopyToUser(UserOut, &Snap, sizeof(Snap)) < 0) {
        return -1;
    }
    return 0;
}

