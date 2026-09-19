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
