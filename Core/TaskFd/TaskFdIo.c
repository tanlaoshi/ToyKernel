/*
 * TaskFdIo.c — read / write / seek（PR-S-taskfd-1）
 */
#include "TaskFd.h"
#include "TaskFdPrivate.h"
#include "Scheduler.h"
#include "CoreOps.h"
#include "Fat.h"
#include "PhysicalMemory.h"
#include "LwIp.h"
#include "Socket.h"
#include "Errno.h"
#include "Console.h"
#include "HalConsole.h"

int SchedulerFdRead(TASK *T, int Fd, void *Buf, UINTN Len) {
    TASK_FD *F;
    UINTN N;
    UINTN i;
    int Ret;

    if (!T || !Buf || Fd < 0 || Fd >= MAX_FDS || !T->Fds[Fd].Used) {
        return -1;
    }
    F = &T->Fds[Fd];
    if (F->Kind == FD_KIND_CONSOLE) {
        /* stdin 占位：此前落进 FILE 分支 Size=0 → 立刻 EOF（chat: bye） */
        if (Fd != 0 || Len == 0) {
            return -1;
        }
        for (i = 0; i < Len; i++) {
            char C = ConsoleStdinGetChar();
            /* 串口终端常发 CR；统一成 LF，与 shell SerialIsEnter 一致 */
            if (C == '\r') {
                C = '\n';
            }
            /* 串口回显（\\n → \\r\\n），否则 chat> 后看不见自己打的字 */
            HalConsolePutChar(C);
            ((char *)Buf)[i] = C;
        }
        return (int)Len;
    }
    if (F->Kind == FD_KIND_SOCKET) {
        /* 0=一直等到有数据/EOF；曾 2000 超时返回 0 被当成 EOF，聊天必断 */
        Ret = LwIpSocketRecv(F->SockId, Buf, Len, 0);
        if (Ret == -2) {
            return 0; /* EOF */
        }
        return Ret;
    }
    if (F->Kind == FD_KIND_PIPE) {
        PIPE *P = PipeFromFd(F);
        if (F->SockId != PIPE_END_READ || !P) {
            return -1;
        }
        if (P->Len == 0) {
            return 0; /* 无数据：无写端则为 EOF；有写端则暂返回 0 */
        }
        N = P->Len;
        if (N > Len) {
            N = Len;
        }
        for (i = 0; i < N; i++) {
            ((UINT8 *)Buf)[i] = P->Buf[P->Head];
            P->Head++;
            if (P->Head >= P->Cap) {
                P->Head = 0;
            }
        }
        P->Len -= N;
        return (int)N;
    }
    if (F->Kind == FD_KIND_DIR) {
        return -1;
    }
    /* FD_KIND_FILE：按偏移从盘读 */
    if (F->Pos >= F->Size) {
        return 0;
    }
    N = F->Size - F->Pos;
    if (N > Len) {
        N = Len;
    }
    if (N == 0) {
        return 0;
    }
    {
        UINTN Got = 0;
        if (VfsServiceReadFileAt(F->Path, F->Pos, Buf, N, &Got) != FAT_OK) {
            return -1;
        }
        F->Pos += Got;
        return (int)Got;
    }
}

int SchedulerFdWrite(TASK *T, int Fd, const void *Buf, UINTN Len) {
    TASK_FD *F;
    UINTN i;

    if (!T || !Buf || Fd < 0 || Fd >= MAX_FDS || !T->Fds[Fd].Used) {
        return -1;
    }
    F = &T->Fds[Fd];
    if (F->Kind == FD_KIND_SOCKET) {
        return LwIpSocketSend(F->SockId, Buf, Len);
    }
    if (F->Kind == FD_KIND_PIPE) {
        PIPE *P = PipeFromFd(F);
        UINTN N;
        if (F->SockId != PIPE_END_WRITE || !P) {
            return -1;
        }
        if (P->Readers <= 0) {
            return -1; /* EPIPE */
        }
        N = P->Cap - P->Len;
        if (N > Len) {
            N = Len;
        }
        for (i = 0; i < N; i++) {
            P->Buf[P->Tail] = ((const UINT8 *)Buf)[i];
            P->Tail++;
            if (P->Tail >= P->Cap) {
                P->Tail = 0;
            }
        }
        P->Len += N;
        return (int)N;
    }
    if (F->Kind == FD_KIND_DIR) {
        return -1;
    }
    /* FD_KIND_FILE：按偏移写穿盘；上限 FAT_WRITE_MAX */
    if (F->Pos >= FAT_WRITE_MAX) {
        return -1;
    }
    if (F->Pos + Len > FAT_WRITE_MAX) {
        Len = FAT_WRITE_MAX - F->Pos;
    }
    if (Len == 0) {
        return 0;
    }
    {
        UINTN Got = 0;
        if (VfsServiceWriteFileAt(F->Path, F->Pos, Buf, Len, &Got) != FAT_OK) {
            return -1;
        }
        F->Pos += Got;
        if (F->Pos > F->Size) {
            F->Size = F->Pos;
        }
        F->Dirty = 0;
        return (int)Got;
    }
}

INT64 SchedulerFdSeek(TASK *T, int Fd, INT64 Offset, int Whence) {
    TASK_FD *F;
    INT64 Base;
    INT64 Neu;

    if (!T || Fd < 0 || Fd >= MAX_FDS || !T->Fds[Fd].Used) {
        return -(INT64)TOY_EBADF;
    }
    F = &T->Fds[Fd];
    if (F->Kind != FD_KIND_FILE) {
        return -(INT64)TOY_ESPIPE;
    }
    if (Whence == 0) {
        Base = 0;
    } else if (Whence == 1) {
        Base = (INT64)F->Pos;
    } else if (Whence == 2) {
        Base = (INT64)F->Size;
    } else {
        return -(INT64)TOY_EINVAL;
    }
    Neu = Base + Offset;
    if (Neu < 0) {
        return -(INT64)TOY_EINVAL;
    }
    F->Pos = (UINTN)Neu;
    return Neu;
}
