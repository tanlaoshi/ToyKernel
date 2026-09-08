/*
 * LwIp.c — lwIP 初始化与轮询（NO_SYS）
 */
#include "LwIp.h"
#include "Hal.h"
#include "Debug.h"
#include "Tcp.h"
#include "Udp.h"

#ifdef TOY_LWIP

#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/sys.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "toy_netif.h"
#include "toy_ping.h"
#include "toy_socket.h"
#include "toy_ip.h"
#include "Errno.h"
#include "HalDevices.h"

#define TOY_LWIP_MASK  0xFFFFFF00U  /* 255.255.255.0 */
#define TOY_LWIP_GW    0x0A000202U  /* 10.0.2.2 */
#define TOY_LWIP_DNS   0x0A000203U  /* 10.0.2.3 QEMU SLIRP */

static int gLwIpReady;
static u32_t gLwIpMs;

static volatile int gDnsDone;
static volatile err_t gDnsErr;
static ip_addr_t gDnsAddr;

u32_t sys_now(void) {
    return gLwIpMs;
}

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

int LwIpInit(void) {
    UINT64 IrqFlags;
    ip_addr_t DnsServer;
    ip4_addr_t Dns4;

    if (!HalNetReady()) {
        return -1;
    }
    IrqFlags = HalIrqSave();
    TcpInit();
    UdpInit();
    lwip_init();
    if (ToyNetifAdd(HalNetGetIpAddress(), TOY_LWIP_MASK, TOY_LWIP_GW) != 0) {
        HalIrqRestore(IrqFlags);
        return -1;
    }
    ToyHostIpToLwIp(TOY_LWIP_DNS, &Dns4);
    ip_addr_copy_from_ip4(DnsServer, Dns4);
    dns_setserver(0, &DnsServer);
    HalNetSetLwipReceive(1);
    gLwIpReady = 1;
    HalIrqRestore(IrqFlags);
    DebugWrite("lwip: up (dns 10.0.2.3)\n");
    return 0;
}

void LwIpPoll(void) {
    UINT64 IrqFlags;

    if (!gLwIpReady) {
        return;
    }
    IrqFlags = HalIrqSave();
    gLwIpMs++;
    sys_check_timeouts();
    HalIrqRestore(IrqFlags);
}

/* 一次关中断内完成收发包 + lwIP 定时器，避免 NO_SYS 重入打坏 pbuf */
void LwIpService(void) {
    UINT64 IrqFlags;

    IrqFlags = HalIrqSave();
    HalNetPoll();
    if (gLwIpReady) {
        gLwIpMs++;
        sys_check_timeouts();
    }
    HalIrqRestore(IrqFlags);
}

int LwIpActive(void) {
    return gLwIpReady;
}

int LwIpPing(UINT32 DstIp, int TimeoutMs) {
    return ToyPing(DstIp, TimeoutMs);
}

int LwIpSocketCreate(void) {
    if (!gLwIpReady && LwIpInit() != 0) {
        return -1;
    }
    return ToySocketCreate();
}

int LwIpSocketBind(int Sock, UINT32 Ip, UINT16 Port) {
    if (!gLwIpReady) {
        return -1;
    }
    return ToySocketBind(Sock, Ip, Port);
}

int LwIpSocketListen(int Sock, int Backlog) {
    if (!gLwIpReady) {
        return -1;
    }
    return ToySocketListen(Sock, Backlog);
}

int LwIpSocketAccept(int Sock, int TimeoutMs) {
    if (!gLwIpReady) {
        return -1;
    }
    return ToySocketAccept(Sock, TimeoutMs);
}

int LwIpSocketConnect(int Sock, UINT32 DstIp, UINT16 DstPort) {
    if (!gLwIpReady) {
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
    if (!gLwIpReady && LwIpInit() != 0) {
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
    while (!gDnsDone && Tries-- > 0) {
        LwIpService();
        HalCpuHalt();
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

int LwIpInit(void) {
    return -1;
}

void LwIpPoll(void) {
}

void LwIpService(void) {
    HalNetPoll();
}

int LwIpActive(void) {
    return 0;
}

int LwIpPing(UINT32 DstIp, int TimeoutMs) {
    (void)DstIp;
    (void)TimeoutMs;
    return -1;
}

int LwIpTcpListen(UINT16 Port) {
    (void)Port;
    return -1;
}

int LwIpTcpListenStop(void) {
    return -1;
}

UINT16 LwIpTcpListenPort(void) {
    return 0;
}

int LwIpUdpBind(UINT16 Port) {
    (void)Port;
    return -1;
}

UINT16 LwIpUdpBoundPort(void) {
    return 0;
}

int LwIpUdpSend(UINT32 DstIp, UINT16 DstPort, const void *Data, UINTN Len) {
    (void)DstIp;
    (void)DstPort;
    (void)Data;
    (void)Len;
    return -1;
}

int LwIpUdpRecv(UDP_DATAGRAM *Out) {
    (void)Out;
    return 0;
}

int LwIpTcpConnectSend(UINT32 DstIp, UINT16 DstPort,
                       const void *Data, UINTN Len, int TimeoutMs) {
    (void)DstIp;
    (void)DstPort;
    (void)Data;
    (void)Len;
    (void)TimeoutMs;
    return -1;
}

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
