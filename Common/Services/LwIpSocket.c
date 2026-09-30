/*
 * LwIpSocket.c — lwIP socket / DNS 胶水（PR-S3-lwip-1）
 *
 * 从 LwIp.c 原样搬家；不改语义。核心 init/poll 见 LwIp.c。
 */
#include "LwIp.h"
#include "Hal.h"
#include "Scheduler.h"

#ifdef TOY_LWIP

#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "toy_socket.h"
#include "toy_ip.h"
#include "Errno.h"
#include "HalDevices.h"

static volatile int gDnsDone;
static volatile err_t gDnsErr;
static ip_addr_t gDnsAddr;

static void LwIpDnsFound(const char *Name, const ip_addr_t *Addr, void *Arg) {
    (void)Name;
    (void)Arg;
    if (Addr != NULL) {
        ip_addr_copy(gDnsAddr, *Addr);
        gDnsErr = ERR_OK;
    } else {
        gDnsErr = ERR_VAL;
    }
    gDnsDone = 1;
}

int LwIpSocketCreate(void) {
    if (!LwIpActive() && LwIpInit() != 0) {
        return -1;
    }
    return ToySocketCreate();
}

int LwIpSocketBind(int Sock, UINT32 Ip, UINT16 Port) {
    if (!LwIpActive()) {
        return -1;
    }
    return ToySocketBind(Sock, Ip, Port);
}

int LwIpSocketListen(int Sock, int Backlog) {
    if (!LwIpActive()) {
        return -1;
    }
    return ToySocketListen(Sock, Backlog);
}

int LwIpSocketAccept(int Sock, int TimeoutMs) {
    if (!LwIpActive()) {
        return -1;
    }
    return ToySocketAccept(Sock, TimeoutMs);
}

int LwIpSocketConnect(int Sock, UINT32 DstIp, UINT16 DstPort) {
    if (!LwIpActive()) {
        return -1;
    }
    return ToySocketConnect(Sock, DstIp, DstPort, 8000);
}

int LwIpSocketSend(int Sock, const void *Data, UINTN Len) {
    return ToySocketSend(Sock, Data, Len);
}

int LwIpSocketRecv(int Sock, void *Buf, UINTN Len, int TimeoutMs) {
    return ToySocketRecv(Sock, Buf, Len, TimeoutMs);
}

int LwIpSocketClose(int Sock) {
    return ToySocketClose(Sock);
}

/* PR-N-dns：点分字面量或 lwIP DNS A 记录；成功 0 且 *OutIp 主机序 */
int LwIpDnsLookup(const char *Name, UINT32 *OutIp, int TimeoutMs) {
    err_t Err;
    int Tries;
    ip_addr_t Addr;

    if (!Name || !Name[0] || !OutIp) {
        return -TOY_EINVAL;
    }
    if (HalNetParseIp(Name, OutIp) == 0) {
        return 0;
    }
    if (!LwIpActive() && LwIpInit() != 0) {
        return -TOY_ENETUNREACH;
    }
    gDnsDone = 0;
    gDnsErr = ERR_INPROGRESS;
    ip_addr_set_zero_ip4(&Addr);
    Err = dns_gethostbyname(Name, &Addr, LwIpDnsFound, 0);
    if (Err == ERR_OK) {
        *OutIp = ToyLwIpToHost(ip_2_ip4(&Addr));
        return 0;
    }
    if (Err != ERR_INPROGRESS) {
        return -TOY_EINVAL;
    }
    Tries = TimeoutMs > 0 ? TimeoutMs : 5000;
    HalIrqEnable();
    while (!gDnsDone && Tries-- > 0) {
        LwIpService();
        SchedulerIoBreath(); /* 与 ping/dhcp：勿空转饿死键鼠 */
    }
    if (!gDnsDone) {
        return -TOY_ETIMEDOUT;
    }
    if (gDnsErr != ERR_OK) {
        return -TOY_ENOENT;
    }
    *OutIp = ToyLwIpToHost(ip_2_ip4(&gDnsAddr));
    return 0;
}

#else

int LwIpSocketCreate(void) {
    return -1;
}

int LwIpSocketBind(int Sock, UINT32 Ip, UINT16 Port) {
    (void)Sock;
    (void)Ip;
    (void)Port;
    return -1;
}

int LwIpSocketListen(int Sock, int Backlog) {
    (void)Sock;
    (void)Backlog;
    return -1;
}

int LwIpSocketAccept(int Sock, int TimeoutMs) {
    (void)Sock;
    (void)TimeoutMs;
    return -1;
}

int LwIpSocketConnect(int Sock, UINT32 DstIp, UINT16 DstPort) {
    (void)Sock;
    (void)DstIp;
    (void)DstPort;
    return -1;
}

int LwIpSocketSend(int Sock, const void *Data, UINTN Len) {
    (void)Sock;
    (void)Data;
    (void)Len;
    return -1;
}

int LwIpSocketRecv(int Sock, void *Buf, UINTN Len, int TimeoutMs) {
    (void)Sock;
    (void)Buf;
    (void)Len;
    (void)TimeoutMs;
    return -1;
}

int LwIpSocketClose(int Sock) {
    (void)Sock;
    return -1;
}

int LwIpDnsLookup(const char *Name, UINT32 *OutIp, int TimeoutMs) {
    (void)Name;
    (void)OutIp;
    (void)TimeoutMs;
    return -1;
}

#endif
