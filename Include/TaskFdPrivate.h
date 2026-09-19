/*
 * TaskFdPrivate.h — FD 内部（仅 Common/Core/TaskFd；User 勿 include）
 */
#ifndef TASK_FD_PRIVATE_H
#define TASK_FD_PRIVATE_H

#include "Scheduler.h"

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

static inline PIPE *PipeFromFd(TASK_FD *F) {
    return (PIPE *)(UINTN)F->Data;
}

int FdAllocSlot(TASK *T);

#endif
