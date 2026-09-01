/*
 * Syscall.c — int 0x80 系统调用（用户 Ring 3 可触发）
 */
#include "Syscall.h"
#include "hal.h"
#include "Console.h"
#include "Scheduler.h"
#include "Debug.h"
#include "VirtualMemory.h"

extern void Isr128(void);

#define COPY_BUF_MAX 256
#define PATH_MAX_LEN 15

void SyscallInit(void) {
    HalIdtSetGate(VEC_SYSCALL, (void *)Isr128, 0xEE);
    DebugWrite("syscall: vector 0x80 (DPL=3) ready\n");
}

static int SysWrite(int Fd, UINT64 UserBuf, UINTN Len) {
    char Buf[COPY_BUF_MAX + 1];
    UINTN Done = 0;

    (void)Fd;
    while (Done < Len) {
        UINTN Chunk = Len - Done;
        if (Chunk > COPY_BUF_MAX) {
            Chunk = COPY_BUF_MAX;
        }
        if (VirtualMemoryCopyFromUser(Buf, UserBuf + Done, Chunk) < 0) {
            return Done > 0 ? (int)Done : -1;
        }
        Buf[Chunk] = '\0';
        ConsoleWrite(Buf);
        Done += Chunk;
    }
    return (int)Len;
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

UINT64 SyscallDispatch(struct INT_FRAME *Frame) {
    UINT64 Ret = 0;

    /* 保持 IF=0 直到 iretq 恢复用户 RFLAGS，避免在返回路径嵌套定时器抢占 */
    HalIrqDisable();

    switch (Frame->Rax) {
    case SYS_EXIT:
        Ret = SchedulerExitUser(Frame);
        break;
    case SYS_WRITE:
        Frame->Rax = (UINT64)(long)SysWrite(
            (int)Frame->Rdi, Frame->Rsi, (UINTN)Frame->Rdx);
        break;
    case SYS_OPEN:
        Frame->Rax = (UINT64)(long)SysOpen(Frame->Rdi);
        break;
    case SYS_READ:
        Frame->Rax = (UINT64)(long)SysRead(
            (int)Frame->Rdi, Frame->Rsi, (UINTN)Frame->Rdx);
        break;
    case SYS_CLOSE:
        Frame->Rax = (UINT64)(long)SysClose((int)Frame->Rdi);
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
    default:
        ConsoleWrite("syscall: unknown ");
        ConsoleHex64(Frame->Rax);
        ConsoleWrite("\n");
        Frame->Rax = (UINT64)-1;
        break;
    }

    return Ret;
}
