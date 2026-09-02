/*
 * LwIp.c — lwIP 初始化与轮询（NO_SYS）
 */
#include "LwIp.h"
#include "Hal.h"
#include "Debug.h"

#ifdef TOY_LWIP

#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/sys.h"
#include "toy_netif.h"

#define TOY_LWIP_MASK  0xFFFFFF00U  /* 255.255.255.0 */
#define TOY_LWIP_GW    0x0A000202U  /* 10.0.2.2 */

static int gLwIpReady;
static u32_t gLwIpMs;

u32_t sys_now(void) {
    return gLwIpMs;
}

int LwIpInit(void) {
    if (!HalNetReady()) {
        return -1;
    }
    lwip_init();
    if (ToyNetifAdd(HalNetGetIp(), TOY_LWIP_MASK, TOY_LWIP_GW) != 0) {
        return -1;
    }
    HalNetSetLwIpRx(1);
    gLwIpReady = 1;
    DebugWrite("lwip: up\n");
    return 0;
}

void LwIpPoll(void) {
    if (!gLwIpReady) {
        return;
    }
    gLwIpMs++;
    sys_check_timeouts();
}

int LwIpActive(void) {
    return gLwIpReady;
}

int LwIpPing(UINT32 DstIp, int TimeoutMs) {
    (void)DstIp;
    (void)TimeoutMs;
    return -1;
}

#else

int LwIpInit(void) {
    return -1;
}

void LwIpPoll(void) {
}

int LwIpActive(void) {
    return 0;
}

int LwIpPing(UINT32 DstIp, int TimeoutMs) {
    (void)DstIp;
    (void)TimeoutMs;
    return -1;
}

#endif
