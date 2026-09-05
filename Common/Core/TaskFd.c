/*
 * TaskFd.c — 每任务 FD / 管道 / 套接字（PR-R3 自 Scheduler.c 拆出）
 */
#include "Scheduler.h"
#include "FileSystem.h"
#include "Fat.h"
#include "PhysicalMemory.h"
#include "LwIp.h"
#include "Socket.h"

void TaskClearFds(TASK *T) {
    int i;
    for (i = 0; i < MAX_FDS; i++) {
        T->Fds[i].Used = 0;
        T->Fds[i].Kind = FD_KIND_FILE;
        T->Fds[i].SockId = -1;
        T->Fds[i].Data = 0;
        T->Fds[i].Size = 0;
        T->Fds[i].Pos = 0;
        T->Fds[i].Pages = 0;
        T->Fds[i].Path[0] = 0;
        T->Fds[i].Dirty = 0;
    }
}

/* 管道对象放在单页前部，后随环形缓冲 */
typedef struct {
    UINTN Cap;
    UINTN Head;
    UINTN Tail;
    UINTN Len;
    int Readers;
    int Writers;
    UINT32 Pages;
    UINT8 *Buf;
} PIPE;

static PIPE *PipeFromFd(TASK_FD *F) {
    return (PIPE *)(UINTN)F->Data;
}

static void FdFlush(TASK_FD *F) {
    if (F->Used && F->Kind == FD_KIND_FILE && F->Dirty && F->Path[0] && F->Data) {
        (void)FsWriteFile(F->Path, F->Data, F->Size);
        F->Dirty = 0;
    }
}

static void FdCopyPath(TASK_FD *F, const char *Path) {
    int i;
    for (i = 0; i < (int)sizeof(F->Path) - 1 && Path[i]; i++) {
        F->Path[i] = Path[i];
    }
    F->Path[i] = 0;
}

static int FdAllocSlot(TASK *T) {
    int i;
    for (i = 0; i < MAX_FDS; i++) {
        if (!T->Fds[i].Used) {
            return i;
        }
    }
    return -1;
}

void TaskCloneFds(TASK *Child, TASK *Parent) {
    int i;

    TaskClearFds(Child);
    for (i = 0; i < MAX_FDS; i++) {
        TASK_FD *S = &Parent->Fds[i];
        TASK_FD *D = &Child->Fds[i];
        if (!S->Used || S->Kind != FD_KIND_PIPE) {
            continue;
        }
        *D = *S;
        {
            PIPE *P = PipeFromFd(S);
            if (S->SockId == PIPE_END_READ) {
                P->Readers++;
            } else {
                P->Writers++;
            }
        }
    }
}

void SchedulerFdCloseAll(TASK *T) {
    int i;
    if (!T) {
        return;
    }
    for (i = 0; i < MAX_FDS; i++) {
        if (T->Fds[i].Used) {
            SchedulerFdClose(T, i);
        }
    }
}

int SchedulerFdOpen(TASK *T, const char *Path) {
    int Slot;
    UINT32 Pages;
    void *Buf;
    UINTN Size = 0;

    if (!T || !Path) {
        return -1;
    }
    Slot = FdAllocSlot(T);
    if (Slot < 0) {
        return -1;
    }
    Pages = (FD_MAX_BYTES + PAGE_SIZE - 1) / PAGE_SIZE;
    Buf = PhysicalMemoryAllocatePages(Pages);
    if (!Buf) {
        return -1;
    }
    if (FsReadFile(Path, Buf, FD_MAX_BYTES, &Size) != FAT_OK) {
        Size = 0;
    }
    T->Fds[Slot].Used = 1;
    T->Fds[Slot].Kind = FD_KIND_FILE;
    T->Fds[Slot].SockId = -1;
    T->Fds[Slot].Data = (UINT8 *)Buf;
    T->Fds[Slot].Size = Size;
    T->Fds[Slot].Pos = 0;
    T->Fds[Slot].Pages = Pages;
    FdCopyPath(&T->Fds[Slot], Path);
    T->Fds[Slot].Dirty = 0;
    return Slot;
}

int SchedulerFdSocket(TASK *T, int Domain, int Type, int Protocol) {
    int Slot = -1;
    int Sock;
    int i;

    (void)Protocol;
    if (!T || Domain != AF_INET || Type != SOCK_STREAM) {
        return -1;
    }
    for (i = 0; i < MAX_FDS; i++) {
        if (!T->Fds[i].Used) {
            Slot = i;
            break;
        }
    }
    if (Slot < 0) {
        return -1;
    }
    Sock = LwIpSocketCreate();
    if (Sock < 0) {
        return -1;
    }
    T->Fds[Slot].Used = 1;
    T->Fds[Slot].Kind = FD_KIND_SOCKET;
    T->Fds[Slot].SockId = Sock;
    T->Fds[Slot].Data = 0;
    T->Fds[Slot].Size = 0;
    T->Fds[Slot].Pos = 0;
    T->Fds[Slot].Pages = 0;
    T->Fds[Slot].Path[0] = 0;
    T->Fds[Slot].Dirty = 0;
    return Slot;
}

int SchedulerFdConnect(TASK *T, int Fd, UINT32 Ip, UINT16 Port) {
    if (!T || Fd < 0 || Fd >= MAX_FDS || !T->Fds[Fd].Used) {
        return -1;
    }
    if (T->Fds[Fd].Kind != FD_KIND_SOCKET) {
        return -1;
    }
    return LwIpSocketConnect(T->Fds[Fd].SockId, Ip, Port);
}

int SchedulerFdBind(TASK *T, int Fd, UINT32 Ip, UINT16 Port) {
    if (!T || Fd < 0 || Fd >= MAX_FDS || !T->Fds[Fd].Used) {
        return -1;
    }
    if (T->Fds[Fd].Kind != FD_KIND_SOCKET) {
        return -1;
    }
    return LwIpSocketBind(T->Fds[Fd].SockId, Ip, Port);
}

int SchedulerFdListen(TASK *T, int Fd, int Backlog) {
    if (!T || Fd < 0 || Fd >= MAX_FDS || !T->Fds[Fd].Used) {
        return -1;
    }
    if (T->Fds[Fd].Kind != FD_KIND_SOCKET) {
        return -1;
    }
    return LwIpSocketListen(T->Fds[Fd].SockId, Backlog);
}

int SchedulerFdAccept(TASK *T, int Fd) {
    int Slot = -1;
    int Sock;
    int i;

    if (!T || Fd < 0 || Fd >= MAX_FDS || !T->Fds[Fd].Used) {
        return -1;
    }
    if (T->Fds[Fd].Kind != FD_KIND_SOCKET) {
        return -1;
    }
    for (i = 0; i < MAX_FDS; i++) {
        if (!T->Fds[i].Used) {
            Slot = i;
            break;
        }
    }
    if (Slot < 0) {
        return -1;
    }
    Sock = LwIpSocketAccept(T->Fds[Fd].SockId, 0); /* 0 = 一直等到有连接 */
    if (Sock < 0) {
        return -1;
    }
    T->Fds[Slot].Used = 1;
    T->Fds[Slot].Kind = FD_KIND_SOCKET;
    T->Fds[Slot].SockId = Sock;
    T->Fds[Slot].Data = 0;
    T->Fds[Slot].Size = 0;
    T->Fds[Slot].Pos = 0;
    T->Fds[Slot].Pages = 0;
    T->Fds[Slot].Path[0] = 0;
    T->Fds[Slot].Dirty = 0;
    return Slot;
}

int SchedulerFdRead(TASK *T, int Fd, void *Buf, UINTN Len) {
    TASK_FD *F;
    UINTN N;
    UINTN i;
    int Ret;

    if (!T || !Buf || Fd < 0 || Fd >= MAX_FDS || !T->Fds[Fd].Used) {
        return -1;
    }
    F = &T->Fds[Fd];
    if (F->Kind == FD_KIND_SOCKET) {
        Ret = LwIpSocketRecv(F->SockId, Buf, Len, 2000);
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
    if (F->Pos >= F->Size) {
        return 0;
    }
    N = F->Size - F->Pos;
    if (N > Len) {
        N = Len;
    }
    for (i = 0; i < N; i++) {
        ((UINT8 *)Buf)[i] = F->Data[F->Pos + i];
    }
    F->Pos += N;
    return (int)N;
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
    if (F->Pos > FD_MAX_BYTES) {
        return -1;
    }
    if (F->Pos + Len > FD_MAX_BYTES) {
        Len = FD_MAX_BYTES - F->Pos;
    }
    if (Len == 0) {
        return 0;
    }
    for (i = 0; i < Len; i++) {
        F->Data[F->Pos + i] = ((const UINT8 *)Buf)[i];
    }
    F->Pos += Len;
    if (F->Pos > F->Size) {
        F->Size = F->Pos;
    }
    F->Dirty = 1;
    return (int)Len;
}

int SchedulerFdClose(TASK *T, int Fd) {
    if (!T || Fd < 0 || Fd >= MAX_FDS || !T->Fds[Fd].Used) {
        return -1;
    }
    if (T->Fds[Fd].Kind == FD_KIND_SOCKET) {
        LwIpSocketClose(T->Fds[Fd].SockId);
    } else if (T->Fds[Fd].Kind == FD_KIND_PIPE) {
        PIPE *P = PipeFromFd(&T->Fds[Fd]);
        if (P) {
            if (T->Fds[Fd].SockId == PIPE_END_READ) {
                if (P->Readers > 0) {
                    P->Readers--;
                }
            } else if (P->Writers > 0) {
                P->Writers--;
            }
            if (P->Readers <= 0 && P->Writers <= 0) {
                PhysicalMemoryFreePages(P, P->Pages ? P->Pages : 1);
            }
        }
    } else {
        FdFlush(&T->Fds[Fd]);
        if (T->Fds[Fd].Data) {
            PhysicalMemoryFreePages(T->Fds[Fd].Data, T->Fds[Fd].Pages);
        }
    }
    T->Fds[Fd].Used = 0;
    T->Fds[Fd].Kind = FD_KIND_FILE;
    T->Fds[Fd].SockId = -1;
    T->Fds[Fd].Data = 0;
    T->Fds[Fd].Size = 0;
    T->Fds[Fd].Pos = 0;
    T->Fds[Fd].Pages = 0;
    T->Fds[Fd].Path[0] = 0;
    T->Fds[Fd].Dirty = 0;
    return 0;
}

int SchedulerFdPipe(TASK *T, int PipeFd[2]) {
    int R = -1;
    int W = -1;
    void *Page;
    PIPE *P;
    UINTN Hdr;

    if (!T || !PipeFd) {
        return -1;
    }
    R = FdAllocSlot(T);
    if (R < 0) {
        return -1;
    }
    T->Fds[R].Used = 1; /* 暂占，便于再找写端 */
    W = FdAllocSlot(T);
    if (W < 0) {
        T->Fds[R].Used = 0;
        return -1;
    }

    Page = PhysicalMemoryAllocatePage();
    if (!Page) {
        T->Fds[R].Used = 0;
        return -1;
    }
    {
        UINT8 *B = (UINT8 *)Page;
        UINTN i;
        for (i = 0; i < PAGE_SIZE; i++) {
            B[i] = 0;
        }
    }
    P = (PIPE *)Page;
    Hdr = (sizeof(PIPE) + 15) & ~15ULL;
    if (Hdr >= PAGE_SIZE) {
        PhysicalMemoryFreePage(Page);
        T->Fds[R].Used = 0;
        return -1;
    }
    P->Buf = (UINT8 *)Page + Hdr;
    P->Cap = PAGE_SIZE - Hdr;
    P->Head = 0;
    P->Tail = 0;
    P->Len = 0;
    P->Readers = 1;
    P->Writers = 1;
    P->Pages = 1;

    T->Fds[R].Used = 1;
    T->Fds[R].Kind = FD_KIND_PIPE;
    T->Fds[R].SockId = PIPE_END_READ;
    T->Fds[R].Data = (UINT8 *)(UINTN)P;
    T->Fds[R].Size = 0;
    T->Fds[R].Pos = 0;
    T->Fds[R].Pages = 0;
    T->Fds[R].Path[0] = 0;
    T->Fds[R].Dirty = 0;

    T->Fds[W].Used = 1;
    T->Fds[W].Kind = FD_KIND_PIPE;
    T->Fds[W].SockId = PIPE_END_WRITE;
    T->Fds[W].Data = (UINT8 *)(UINTN)P;
    T->Fds[W].Size = 0;
    T->Fds[W].Pos = 0;
    T->Fds[W].Pages = 0;
    T->Fds[W].Path[0] = 0;
    T->Fds[W].Dirty = 0;

    PipeFd[0] = R;
    PipeFd[1] = W;
    return 0;
}

int SchedulerFdDup(TASK *T, int OldFd) {
    int Slot;
    TASK_FD *S;

    if (!T || OldFd < 0 || OldFd >= MAX_FDS || !T->Fds[OldFd].Used) {
        return -1;
    }
    S = &T->Fds[OldFd];
    if (S->Kind != FD_KIND_PIPE) {
        return -1; /* P2：仅支持 dup 管道端 */
    }
    Slot = FdAllocSlot(T);
    if (Slot < 0) {
        return -1;
    }
    T->Fds[Slot] = *S;
    {
        PIPE *P = PipeFromFd(S);
        if (S->SockId == PIPE_END_READ) {
            P->Readers++;
        } else {
            P->Writers++;
        }
    }
    return Slot;
}

