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
#include "toy_netif.h"
#include "toy_ping.h"
#include "toy_socket.h"

#define TOY_LWIP_MASK  0xFFFFFF00U  /* 255.255.255.0 */
#define TOY_LWIP_GW    0x0A000202U  /* 10.0.2.2 */

static int gLwIpReady;
static u32_t gLwIpMs;

u32_t sys_now(void) {
    return gLwIpMs;
}

int LwIpInit(void) {
    UINT64 IrqFlags;

    if (!HalNetReady()) {
        return -1;
    }
    IrqFlags = HalIrqSave();
    TcpInit();
    UdpInit();
    lwip_init();
    if (ToyNetifAdd(HalNetGetIp(), TOY_LWIP_MASK, TOY_LWIP_GW) != 0) {
        HalIrqRestore(IrqFlags);
        return -1;
    }
    HalNetSetLwIpRx(1);
    gLwIpReady = 1;
    HalIrqRestore(IrqFlags);
    DebugWrite("lwip: up\n");
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

#endif
