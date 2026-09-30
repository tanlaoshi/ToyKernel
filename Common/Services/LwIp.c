/*
 * LwIp.c — lwIP 初始化与轮询（NO_SYS）（PR-S3-lwip-1）
 *
 * socket / DNS 见 LwIpSocket.c。
 */
#include "LwIp.h"
#include "Hal.h"
#include "Debug.h"
#include "Tcp.h"
#include "Udp.h"
#include "NetConfig.h"
#include "LwIpPrivate.h"
#include "Scheduler.h"

#ifdef TOY_LWIP

#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/sys.h"
#include "toy_ping.h"
#include "HalDevices.h"

static int gLwIpReady;
static u32_t gLwIpMs;
/* 软锁：持锁期间保持 IF=1，避免 ping/ARP 时关中断饿死 USB 鼠 */
static volatile UINT32 gLwIpSoft;

u32_t sys_now(void) {
    return gLwIpMs;
}

void LwIpLock(void) {
    while (__sync_lock_test_and_set(&gLwIpSoft, 1u)) {
        SchedulerIoBreath();
    }
}

void LwIpUnlock(void) {
    __sync_lock_release(&gLwIpSoft);
}

int LwIpInit(void) {
    if (!HalNetReady()) {
        return -1;
    }
    NetConfigEnsure();
    HalNetSetIpAddress(NetConfigGetIp());
    LwIpLock();
    TcpInit();
    UdpInit();
    lwip_init();
    if (LwIpConfigBindNetif() != 0) {
        LwIpUnlock();
        return -1;
    }
    LwIpConfigPushDns();
    HalNetSetLwipReceive(1);
    gLwIpReady = 1;
    LwIpUnlock();
    LwIpConfigLogDns();
    return 0;
}

void LwIpPoll(void) {
    if (!gLwIpReady) {
        return;
    }
    if (__sync_lock_test_and_set(&gLwIpSoft, 1u)) {
        return;
    }
    gLwIpMs++;
    sys_check_timeouts();
    __sync_lock_release(&gLwIpSoft);
}

/*
 * NO_SYS：Shell 与 Worker 都可能进来。软锁保持 IF=1；
 * 抢不到锁则本拍跳过（调用方会再转），绝不 SpinLock cli。
 */
void LwIpService(void) {
    HalNetPoll();
    if (!gLwIpReady) {
        return;
    }
    if (__sync_lock_test_and_set(&gLwIpSoft, 1u)) {
        SchedulerIoBreath();
        return;
    }
    gLwIpMs++;
    sys_check_timeouts();
    __sync_lock_release(&gLwIpSoft);
}

int LwIpActive(void) {
    return gLwIpReady;
}

int LwIpPing(UINT32 DstIp, int TimeoutMs) {
    return ToyPing(DstIp, TimeoutMs);
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

void LwIpLock(void) {
}

void LwIpUnlock(void) {
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

#endif
