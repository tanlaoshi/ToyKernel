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
#include "DriverNet.h"

static int gLwIpReady;
/* 软锁：持锁期间保持 IF=1，避免 ping/ARP 时关中断饿死 USB 鼠 */
static volatile UINT32 gLwIpSoft;

/*
 * 墙钟毫秒。旧实现每 LwIpService 自增 1，NUC 上 Halt≈4ms/拍时 TCP RTO=3000
 * 会拖到十余秒；忙泵时又会「时间飞逝」。须跟 HalCpuTicks 对齐。
 */
u32_t sys_now(void) {
    UINT32 Tps = HalTicksPerSec();
    UINT64 T = HalCpuTicks(0);

    if (Tps == 0) {
        return 0;
    }
    return (u32_t)((T * 1000ULL) / (UINT64)Tps);
}

void LwIpLock(void) {
    while (__sync_lock_test_and_set(&gLwIpSoft, 1u)) {
        SchedulerIoBreath();
    }
}

void LwIpUnlock(void) {
    __sync_lock_release(&gLwIpSoft);
}

int LwIpInitialize(void) {
    if (!HalNetReady()) {
        return -1;
    }
    NetConfigEnsure();
    HalNetSetIpAddress(NetConfigGetIp());
    LwIpLock();
    TcpInitialize();
    UdpInitialize();
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
    sys_check_timeouts();
    __sync_lock_release(&gLwIpSoft);
}

/*
 * NO_SYS：Shell 与 Worker 都可能进来。软锁保持 IF=1；
 * 抢不到锁则本拍跳过（调用方会再转），绝不 SpinLock cli。
 *
 * 必须把 NetPoll 也放进同一把锁：否则 Shell 与 HttpGet Worker 并发
 * AlxPoll 会踩 RX 环 → store sync 只发出 GET、收 0 字节（http empty）。
 * 勿调 HalNetPoll（其内部也会抢锁，非递归）。
 */
void LwIpService(void) {
    if (!gLwIpReady) {
        ToyDriverNetPoll();
        return;
    }
    if (__sync_lock_test_and_set(&gLwIpSoft, 1u)) {
        SchedulerIoBreath();
        return;
    }
    ToyDriverNetPoll();
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

int LwIpInitialize(void) {
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
