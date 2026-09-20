/*
 * TaskFdPipe.c — pipe / dup（PR-S-taskfd-1）
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
