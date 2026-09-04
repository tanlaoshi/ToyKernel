/*
 * Syscall.c — 系统调用分发（入口无关）
 *
 * 教学双路径（互不耦合）：
 *   - legacy：int 0x80 → IDT 门 → InterruptDispatch → SyscallDispatch
 *   - 快速：syscall → SyscallEntry → SyscallDispatch → sysretq
 * 号与 ABI（rax / rdi,rsi,rdx）两条路径共用。
 * 向量/MSR 安装在 HalSyscallInit（PR-A1），本文件只做分发。
 */
#include "Syscall.h"
#include "Hal.h"
#include "Console.h"
#include "Scheduler.h"
#include "Debug.h"
#include "VirtualMemory.h"
#include "Process.h"

#define COPY_BUF_MAX 256
/* 与 TASK_FD.Path[64] 对齐，便于 CRT 打开子路径 */
#define PATH_MAX_LEN 63

void SyscallInit(void) {
    /* 硬件入口已由 HalSyscallInit 安装；保留符号供旧调用点 / 文档 */
}

static int SysWrite(int Fd, UINT64 UserBuf, UINTN Len) {
    char Buf[COPY_BUF_MAX + 1];
    UINTN Done = 0;
    TASK *T = SchedulerCurrent();

    if (Fd == 1 || Fd == 2) {
        while (Done < Len) {
            UINTN Chunk = Len - Done;
            if (Chunk > COPY_BUF_MAX) {
                Chunk = COPY_BUF_MAX;
            }
            if (VirtualMemoryCopyFromUser(Buf, UserBuf + Done, Chunk) < 0) {
                return Done > 0 ? (int)Done : -1;
            }
            ConsoleWriteLen(Buf, Chunk);
            Done += Chunk;
        }
        return (int)Len;
    }

    if (!T || !T->IsUser) {
        return -1;
    }
    while (Done < Len) {
        UINTN Chunk = Len - Done;
        int N;

        if (Chunk > COPY_BUF_MAX) {
            Chunk = COPY_BUF_MAX;
        }
        if (VirtualMemoryCopyFromUser(Buf, UserBuf + Done, Chunk) < 0) {
            return Done > 0 ? (int)Done : -1;
        }
        N = SchedulerFdWrite(T, Fd, Buf, Chunk);
        if (N < 0) {
            return Done > 0 ? (int)Done : -1;
        }
        if (N == 0) {
            break;
        }
        Done += (UINTN)N;
    }
    return (int)Done;
}

static int SysOpen(UINT64 UserPath) {
    char Path[PATH_MAX_LEN + 1];
    TASK *T = SchedulerCurrent();
    UINTN i;

    if (!T || !T->IsUser) {
        return -1;
    }
    for (i = 0; i < PATH_MAX_LEN; i++) {
        char C;
        if (VirtualMemoryCopyFromUser(&C, UserPath + i, 1) < 0) {
            return -1;
        }
        Path[i] = C;
        if (C == 0) {
            break;
        }
    }
    Path[PATH_MAX_LEN] = 0;
    if (Path[0] == 0) {
        return -1;
    }
    return SchedulerFdOpen(T, Path);
}

static int SysRead(int Fd, UINT64 UserBuf, UINTN Len) {
    char Buf[COPY_BUF_MAX];
    TASK *T = SchedulerCurrent();
    UINTN Done = 0;
    int N;

    if (!T || !T->IsUser) {
        return -1;
    }
    while (Done < Len) {
        UINTN Chunk = Len - Done;
        if (Chunk > COPY_BUF_MAX) {
            Chunk = COPY_BUF_MAX;
        }
        N = SchedulerFdRead(T, Fd, Buf, Chunk);
        if (N < 0) {
            return Done > 0 ? (int)Done : -1;
        }
        if (N == 0) {
            break;
        }
        if (VirtualMemoryCopyToUser(UserBuf + Done, Buf, (UINTN)N) < 0) {
            return Done > 0 ? (int)Done : -1;
        }
        Done += (UINTN)N;
        if ((UINTN)N < Chunk) {
            break;
        }
    }
    return (int)Done;
}

static int SysClose(int Fd) {
    TASK *T = SchedulerCurrent();
    if (!T || !T->IsUser) {
        return -1;
    }
    return SchedulerFdClose(T, Fd);
}

static int SysSocket(int Domain, int Type, int Protocol) {
    TASK *T = SchedulerCurrent();

    if (!T || !T->IsUser) {
        return -1;
    }
    return SchedulerFdSocket(T, Domain, Type, Protocol);
}

static int SysConnect(int Fd, UINT32 Ip, UINT16 Port) {
    TASK *T = SchedulerCurrent();

    if (!T || !T->IsUser) {
        return -1;
    }
    return SchedulerFdConnect(T, Fd, Ip, Port);
}

static int SysBind(int Fd, UINT32 Ip, UINT16 Port) {
    TASK *T = SchedulerCurrent();

    if (!T || !T->IsUser) {
        return -1;
    }
    return SchedulerFdBind(T, Fd, Ip, Port);
}

static int SysListen(int Fd, int Backlog) {
    TASK *T = SchedulerCurrent();

    if (!T || !T->IsUser) {
        return -1;
    }
    return SchedulerFdListen(T, Fd, Backlog);
}

static int SysAccept(int Fd) {
    TASK *T = SchedulerCurrent();

    if (!T || !T->IsUser) {
        return -1;
    }
    return SchedulerFdAccept(T, Fd);
}

static int SysExecve(HAL_FRAME *Frame, UINT64 UserPath, UINT64 UserArgv,
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

UINT64 SyscallDispatch(HAL_FRAME *Frame) {
    UINT64 Ret = 0;

    /* 保持 IF=0 直到 iretq 恢复用户 RFLAGS，避免在返回路径嵌套定时器抢占 */
    HalIrqDisable();

    switch (HalFrameSyscallNum(Frame)) {
    case SYS_EXIT:
        Ret = SchedulerExitUser(Frame);
        break;
    case SYS_WRITE:
        HalFrameSetReturn(Frame, (UINT64)(long)SysWrite(
            (int)HalFrameArg0(Frame), HalFrameArg1(Frame),
            (UINTN)HalFrameArg2(Frame)));
        break;
    case SYS_OPEN:
        HalFrameSetReturn(Frame, (UINT64)(long)SysOpen(HalFrameArg0(Frame)));
        break;
    case SYS_READ:
        HalFrameSetReturn(Frame, (UINT64)(long)SysRead(
            (int)HalFrameArg0(Frame), HalFrameArg1(Frame),
            (UINTN)HalFrameArg2(Frame)));
        break;
    case SYS_CLOSE:
        HalFrameSetReturn(Frame, (UINT64)(long)SysClose((int)HalFrameArg0(Frame)));
        break;
    case SYS_FORK:
        Ret = SchedulerFork(Frame);
        break;
    case SYS_WAIT:
        Ret = SchedulerWait(Frame);
        break;
    case SYS_YIELD:
        Ret = SchedulerYield(Frame);
        break;
    case SYS_SOCKET:
        HalFrameSetReturn(Frame, (UINT64)(long)SysSocket(
            (int)HalFrameArg0(Frame), (int)HalFrameArg1(Frame),
            (int)HalFrameArg2(Frame)));
        break;
    case SYS_CONNECT:
        HalFrameSetReturn(Frame, (UINT64)(long)SysConnect(
            (int)HalFrameArg0(Frame), (UINT32)HalFrameArg1(Frame),
            (UINT16)HalFrameArg2(Frame)));
        break;
    case SYS_BIND:
        HalFrameSetReturn(Frame, (UINT64)(long)SysBind(
            (int)HalFrameArg0(Frame), (UINT32)HalFrameArg1(Frame),
            (UINT16)HalFrameArg2(Frame)));
        break;
    case SYS_LISTEN:
        HalFrameSetReturn(Frame, (UINT64)(long)SysListen(
            (int)HalFrameArg0(Frame), (int)HalFrameArg1(Frame)));
        break;
    case SYS_ACCEPT:
        HalFrameSetReturn(Frame, (UINT64)(long)SysAccept((int)HalFrameArg0(Frame)));
        break;
    case SYS_EXECVE:
        if (SysExecve(Frame, HalFrameArg0(Frame), HalFrameArg1(Frame),
                      HalFrameArg2(Frame)) != 0) {
            HalFrameSetReturn(Frame, (UINT64)(INT64)-1);
        }
        /* 成功：Frame 已指向新入口，sysret 进新映像 */
        break;
    case SYS_PIPE: {
        TASK *T = SchedulerCurrent();
        int Fds[2];
        UINT64 UserPtr = HalFrameArg0(Frame);
        if (!T || !T->IsUser || SchedulerFdPipe(T, Fds) != 0) {
            HalFrameSetReturn(Frame, (UINT64)(INT64)-1);
            break;
        }
        VirtualMemoryLoadPageTable(T->PageRoot);
        if (VirtualMemoryCopyToUser(UserPtr, Fds, sizeof(Fds)) < 0) {
            VirtualMemoryLoadPageTable(VirtualMemoryKernelRoot());
            SchedulerFdClose(T, Fds[0]);
            SchedulerFdClose(T, Fds[1]);
            HalFrameSetReturn(Frame, (UINT64)(INT64)-1);
            break;
        }
        VirtualMemoryLoadPageTable(VirtualMemoryKernelRoot());
        HalFrameSetReturn(Frame, 0);
        break;
    }
    case SYS_DUP: {
        TASK *T = SchedulerCurrent();
        if (!T || !T->IsUser) {
            HalFrameSetReturn(Frame, (UINT64)(INT64)-1);
            break;
        }
        HalFrameSetReturn(Frame, (UINT64)(long)SchedulerFdDup(
            T, (int)HalFrameArg0(Frame)));
        break;
    }
    case SYS_BRK:
        HalFrameSetReturn(Frame, ProcessBrk(HalFrameArg0(Frame)));
        break;
    case SYS_KILL:
        Ret = SchedulerKill(Frame);
        break;
    default:
        ConsoleWrite("syscall: unknown ");
        ConsoleHex64(HalFrameSyscallNum(Frame));
        ConsoleWrite("\n");
        HalFrameSetReturn(Frame, (UINT64)-1);
        break;
    }

    return Ret;
}
