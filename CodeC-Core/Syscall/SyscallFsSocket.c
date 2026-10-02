/*
 * SyscallFsSocket.c — socket 系统调用（PR-S-syscallfs-1）
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

/* 非阻塞：>0 字节；0=EOF；-TOY_EAGAIN=暂无数据；其它负=错 */
int SysRecvNb(int Fd, UINT64 UserBuf, UINTN Len) {
    char Buf[COPY_BUF_MAX];
    TASK *T = SchedulerCurrent();
    int N;

    if (!T || !T->IsUser || Len == 0) {
        return -1;
    }
    if (Len > COPY_BUF_MAX) {
        Len = COPY_BUF_MAX;
    }
    N = SchedulerFdReadTimeout(T, Fd, Buf, Len, -1);
    if (N == -TOY_EAGAIN) {
        return -TOY_EAGAIN;
    }
    if (N < 0) {
        return -1;
    }
    if (N == 0) {
        return 0;
    }
    if (VirtualMemoryCopyToUser(UserBuf, Buf, (UINTN)N) < 0) {
        return -1;
    }
    return N;
}
