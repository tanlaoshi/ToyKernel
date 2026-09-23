/*
 * SyscallFs.c — 文件与目录系统调用（PR-S-syscallfs-1）
 */
#include "SyscallPrivate.h"
#include "Scheduler.h"
#include "Console.h"
#include "VirtualMemory.h"
#include "Fat.h"
#include "Socket.h"
#include "LwIp.h"
#include "Errno.h"
#include "BootTypes.h"

_Static_assert(sizeof(TOY_NET_DNS_QUERY) == 132, "TOY_NET_DNS_QUERY layout");

int CopyUserCString(char *Dst, UINT64 UserSrc, UINTN MaxLen) {
    UINTN i;

    if (!Dst || MaxLen == 0) {
        return -1;
    }
    for (i = 0; i < MaxLen; i++) {
        char C;
        if (VirtualMemoryCopyFromUser(&C, UserSrc + i, 1) < 0) {
            return -1;
        }
        Dst[i] = C;
        if (C == 0) {
            return 0;
        }
    }
    Dst[MaxLen - 1] = 0;
    return 0;
}

int SysWrite(int Fd, UINT64 UserBuf, UINTN Len) {
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

int SysOpen(UINT64 UserPath) {
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
    if (CwdResolve(T, Path, (int)sizeof(Path)) != 0) {
        return -1;
    }
    return SchedulerFdOpen(T, Path);
}

int SysRead(int Fd, UINT64 UserBuf, UINTN Len) {
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

int SysClose(int Fd) {
    TASK *T = SchedulerCurrent();
    if (!T || !T->IsUser) {
        return -1;
    }
    return SchedulerFdClose(T, Fd);
}

INT64 SysLseek(int Fd, INT64 Offset, int Whence) {
    TASK *T = SchedulerCurrent();

    if (!T || !T->IsUser) {
        return -1;
    }
    return SchedulerFdSeek(T, Fd, Offset, Whence);
}

/* PR-F4：用户态 FileStat / OpenDirectory / ReadDirectory */

int SysFileStat(UINT64 UserPath, UINT64 UserOut) {
    char Path[PATH_MAX_LEN + 1];
    FAT_FILE_STAT St;
    TASK *T = SchedulerCurrent();
    UINTN i;

    if (!T || !T->IsUser || UserOut == 0) {
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
    if (CwdResolve(T, Path, (int)sizeof(Path)) != 0) {
        return -1;
    }
    if (SchedulerFdFileStat(T, Path, &St) < 0) {
        return -1;
    }
    if (VirtualMemoryCopyToUser(UserOut, &St, sizeof(St)) < 0) {
        return -1;
    }
    return 0;
}

/* PR-U-stat：按 fd.Path 填 TOY_FILE_STAT；socket/pipe 失败 */
int SysFstat(int Fd, UINT64 UserOut) {
    TASK *T = SchedulerCurrent();
    TASK_FD *F;
    FAT_FILE_STAT St;

    if (!T || !T->IsUser || UserOut == 0 || Fd < 0 || Fd >= MAX_FDS) {
        return -1;
    }
    F = &T->Fds[Fd];
    if (!F->Used || F->Kind == FD_KIND_SOCKET || F->Kind == FD_KIND_PIPE) {
        return -1;
    }
    if (SchedulerFdFileStat(T, F->Path, &St) < 0) {
        return -1;
    }
    if (VirtualMemoryCopyToUser(UserOut, &St, sizeof(St)) < 0) {
        return -1;
    }
    return 0;
}

int SysOpenDirectory(UINT64 UserPath) {
    char Path[PATH_MAX_LEN + 1];
    TASK *T = SchedulerCurrent();
    UINTN i;

    if (!T || !T->IsUser) {
        return -1;
    }
    /* 空路径 = 默认卷根 */
    if (UserPath == 0) {
        Path[0] = 0;
    } else {
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
    }
    if (Path[0] && CwdResolve(T, Path, (int)sizeof(Path)) != 0) {
        return -1;
    }
    return SchedulerFdOpenDirectory(T, Path);
}

int SysReadDirectory(int Fd, UINT64 UserOut) {
    FAT_DIRECTORY_ENTRY Ent;
    TASK *T = SchedulerCurrent();
    int Rc;

    if (!T || !T->IsUser || UserOut == 0) {
        return -1;
    }
    Rc = SchedulerFdReadDirectory(T, Fd, &Ent);
    if (Rc <= 0) {
        return Rc;
    }
    if (VirtualMemoryCopyToUser(UserOut, &Ent, sizeof(Ent)) < 0) {
        return -1;
    }
    return 1;
}

int SysPipe(UINT64 UserPtr) {
    TASK *T = SchedulerCurrent();
    int Fds[2];

    if (!T || !T->IsUser || SchedulerFdPipe(T, Fds) != 0) {
        return -1;
    }
    VirtualMemoryLoadPageTable(T->PageRoot);
    if (VirtualMemoryCopyToUser(UserPtr, Fds, sizeof(Fds)) < 0) {
        VirtualMemoryLoadPageTable(VirtualMemoryKernelRoot());
        SchedulerFdClose(T, Fds[0]);
        SchedulerFdClose(T, Fds[1]);
        return -1;
    }
    VirtualMemoryLoadPageTable(VirtualMemoryKernelRoot());
    return 0;
}

int SysDup(int Fd) {
    TASK *T = SchedulerCurrent();

    if (!T || !T->IsUser) {
        return -1;
    }
    return SchedulerFdDup(T, Fd);
}

