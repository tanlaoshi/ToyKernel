/*
 * SyscallFs.c — PR-S-syscall-split-1：FD / FS / 网络 socket 系统调用
 */
#include "SyscallPriv.h"
#include "Scheduler.h"
#include "Console.h"
#include "VirtualMemory.h"
#include "Fat.h"
#include "Socket.h"
#include "LwIp.h"
#include "Errno.h"

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
    if (SchedulerFdFileStat(T, Path, &St) < 0) {
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

int SysSocket(int Domain, int Type, UINT64 Protocol) {
    TASK *T = SchedulerCurrent();
    TOY_NET_DNS_QUERY Query;
    UINT32 Ip;
    int Rc;

    if (!T || !T->IsUser) {
        return -1;
    }
    if (Type == TOY_NET_SOCK_RESOLVE) {
        if (Domain != AF_INET || Protocol == 0) {
            return -TOY_EINVAL;
        }
        if (VirtualMemoryCopyFromUser(&Query, Protocol, sizeof(Query)) < 0) {
            return -TOY_EINVAL;
        }
        Query.Name[TOY_NET_NAME_MAX] = 0;
        Rc = LwIpDnsLookup(Query.Name, &Ip, 5000);
        if (Rc != 0) {
            return Rc;
        }
        Query.Ip = Ip;
        if (VirtualMemoryCopyToUser(Protocol, &Query, sizeof(Query)) < 0) {
            return -TOY_EINVAL;
        }
        return 0;
    }
    return SchedulerFdSocket(T, Domain, Type, (int)Protocol);
}

int SysConnect(int Fd, UINT32 Ip, UINT16 Port) {
    TASK *T = SchedulerCurrent();

    if (!T || !T->IsUser) {
        return -1;
    }
    return SchedulerFdConnect(T, Fd, Ip, Port);
}

int SysBind(int Fd, UINT32 Ip, UINT16 Port) {
    TASK *T = SchedulerCurrent();

    if (!T || !T->IsUser) {
        return -1;
    }
    return SchedulerFdBind(T, Fd, Ip, Port);
}

int SysListen(int Fd, int Backlog) {
    TASK *T = SchedulerCurrent();

    if (!T || !T->IsUser) {
        return -1;
    }
    return SchedulerFdListen(T, Fd, Backlog);
}

int SysAccept(int Fd) {
    TASK *T = SchedulerCurrent();

    if (!T || !T->IsUser) {
        return -1;
    }
    return SchedulerFdAccept(T, Fd);
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

