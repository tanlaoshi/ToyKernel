/*
 * TaskFd.c — 槽位、打开与关闭（PR-S-taskfd-1）
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

static void FdFlush(TASK_FD *F) {
    /* PR-U-stream-1：文件写穿盘，无整文件 Dirty 缓冲 */
    (void)F;
}

static void FdCopyPath(TASK_FD *F, const char *Path) {
    int i;
    for (i = 0; i < (int)sizeof(F->Path) - 1 && Path[i]; i++) {
        F->Path[i] = Path[i];
    }
    F->Path[i] = 0;
}

int FdAllocSlot(TASK *T) {
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
    FAT_FILE_STAT St;
    UINTN Size = 0;

    if (!T || !Path) {
        return -1;
    }
    Slot = FdAllocSlot(T);
    if (Slot < 0) {
        return -1;
    }
    /* PR-U-stream-1：仅记路径与长度；缺文件仍开 fd（WRITE 创建） */
    if (VfsServiceFileStat(Path, &St) == FAT_OK) {
        if (St.Attr & FAT_ATTR_DIR) {
            return -1;
        }
        Size = St.Size;
    }
    T->Fds[Slot].Used = 1;
    T->Fds[Slot].Kind = FD_KIND_FILE;
    T->Fds[Slot].SockId = -1;
    T->Fds[Slot].Data = 0;
    T->Fds[Slot].Size = Size;
    T->Fds[Slot].Pos = 0;
    T->Fds[Slot].Pages = 0;
    FdCopyPath(&T->Fds[Slot], Path);
    T->Fds[Slot].Dirty = 0;
    return Slot;
}

/* PR-F4：目录 fd — 打开时 FileSystemListEntries 快照到内核缓冲 */
int SchedulerFdOpenDirectory(TASK *T, const char *Path) {
    int Slot;
    UINT32 Pages;
    FAT_DIRECTORY_ENTRY *Buf;
    FAT_FILE_STAT St;
    int Count = 0;
    int Err;
    const char *ListPath;

    if (!T) {
        return -1;
    }
    ListPath = Path ? Path : "";
    Err = VfsServiceFileStat(ListPath, &St);
    if (Err != FAT_OK) {
        return -1;
    }
    if ((St.Attr & FAT_ATTR_DIR) == 0) {
        return -1;
    }
    Slot = FdAllocSlot(T);
    if (Slot < 0) {
        return -1;
    }
    Pages = (UINT32)((sizeof(FAT_DIRECTORY_ENTRY) * (UINTN)FAT_LIST_MAX + PAGE_SIZE - 1) / PAGE_SIZE);
    if (Pages == 0) {
        Pages = 1;
    }
    Buf = (FAT_DIRECTORY_ENTRY *)PhysicalMemoryAllocatePages(Pages);
    if (!Buf) {
        return -1;
    }
    Err = VfsServiceListEntries(ListPath, Buf, FAT_LIST_MAX, &Count);
    if (Err != FAT_OK) {
        PhysicalMemoryFreePages(Buf, Pages);
        return -1;
    }
    T->Fds[Slot].Used = 1;
    T->Fds[Slot].Kind = FD_KIND_DIR;
    T->Fds[Slot].SockId = -1;
    T->Fds[Slot].Data = (UINT8 *)Buf;
    T->Fds[Slot].Size = (UINTN)Count;
    T->Fds[Slot].Pos = 0;
    T->Fds[Slot].Pages = Pages;
    FdCopyPath(&T->Fds[Slot], ListPath[0] ? ListPath : "/");
    T->Fds[Slot].Dirty = 0;
    return Slot;
}

int SchedulerFdReadDirectory(TASK *T, int Fd, FAT_DIRECTORY_ENTRY *Out) {
    TASK_FD *F;

    if (!T || !Out || Fd < 0 || Fd >= MAX_FDS || !T->Fds[Fd].Used) {
        return -1;
    }
    F = &T->Fds[Fd];
    if (F->Kind != FD_KIND_DIR || !F->Data) {
        return -1;
    }
    if (F->Pos >= F->Size) {
        return 0;
    }
    *Out = ((FAT_DIRECTORY_ENTRY *)(UINTN)F->Data)[F->Pos];
    F->Pos++;
    return 1;
}

int SchedulerFdFileStat(TASK *T, const char *Path, FAT_FILE_STAT *Out) {
    if (!T || !Out) {
        return -1;
    }
    if (VfsServiceFileStat(Path ? Path : "", Out) != FAT_OK) {
        return -1;
    }
    return 0;
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
    } else if (T->Fds[Fd].Kind == FD_KIND_DIR) {
        if (T->Fds[Fd].Data) {
            PhysicalMemoryFreePages(T->Fds[Fd].Data, T->Fds[Fd].Pages);
        }
    } else {
        /* FILE：流式已穿盘；无整文件页 */
        (void)FdFlush(&T->Fds[Fd]);
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
