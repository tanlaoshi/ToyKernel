/*
 * TaskFdSocket.c — 套接字 fd（PR-S-taskfd-1）
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
