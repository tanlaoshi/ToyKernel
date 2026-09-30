/*
 * LwIpDhcp.c — DHCP 客户端（PR-N-nic-dhcp / 前后端分离）
 *
 * Shell：Enqueue + ConsoleJobHoldPrompt（本窗等 IP，可开另一 Shell）。
 * WorkerTask：LwIpDhcpStep；完成行 WriteToJobShell + ReleasePrompt。
 */
#include "LwIp.h"
#include "NetConfig.h"
#include "Hal.h"
#include "HalDevices.h"
#include "Console.h"
#include "Debug.h"

#ifdef TOY_LWIP

#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "toy_netif.h"
#include "toy_ip.h"

typedef enum {
    DHCP_JOB_IDLE = 0,
    DHCP_JOB_RUN,
    DHCP_JOB_OK,
    DHCP_JOB_FAIL
} DHCP_JOB_STATE;

static int gDhcpRunning;
static volatile DHCP_JOB_STATE gJob;
static UINT32 gBudget;
#if defined(__x86_64__)
static UINT64 gDhcpT0;
static UINT64 gDhcpNeed;

static UINT64 DhcpTsc(void) {
    UINT32 Lo;
    UINT32 Hi;

    __asm__ volatile("rdtsc" : "=a"(Lo), "=d"(Hi));
    return ((UINT64)Hi << 32) | Lo;
}
#endif

void LwIpDhcpStop(void) {
    struct netif *Netif;

    if (!gDhcpRunning) {
        return;
    }
    Netif = ToyNetifGet();
    if (Netif) {
        dhcp_release_and_stop(Netif);
    }
    gDhcpRunning = 0;
}

int LwIpDhcpRunning(void) {
    return gDhcpRunning;
}

int LwIpDhcpJobBusy(void) {
    return (gJob == DHCP_JOB_RUN) ? 1 : 0;
}

static void AdoptFromNetif(struct netif *Netif) {
    UINT32 Ip;
    UINT32 Mask;
    UINT32 Gw;
    UINT32 Dns;
    const ip_addr_t *DnsSrv;

    Ip = ToyLwIpToHost(netif_ip4_addr(Netif));
    Mask = ToyLwIpToHost(netif_ip4_netmask(Netif));
    Gw = ToyLwIpToHost(netif_ip4_gw(Netif));
    Dns = 0;
    DnsSrv = dns_getserver(0);
    if (DnsSrv != 0 && !ip_addr_isany(DnsSrv)) {
        Dns = ToyLwIpToHost(ip_2_ip4(DnsSrv));
    }
    NetConfigAdopt(Ip, Mask, Gw, Dns);
}

static void ClearNetifAddr(struct netif *Netif) {
    ip4_addr_t Any;

    ip4_addr_set_any(&Any);
    netif_set_addr(Netif, &Any, &Any, &Any);
}

static void FinishPrint(int Ok) {
    char Msg[80];
    char IpBuf[16];
    int N = 0;
    const char *P;

    if (Ok) {
        HalNetFormatIp(NetConfigGetIp(), IpBuf, (int)sizeof(IpBuf));
        P = "lwip dhcp: ok ip=";
        while (*P && N < 70) {
            Msg[N++] = *P++;
        }
        P = IpBuf;
        while (*P && N < 70) {
            Msg[N++] = *P++;
        }
        P = " gw=";
        while (*P && N < 74) {
            Msg[N++] = *P++;
        }
        HalNetFormatIp(NetConfigGetGw(), IpBuf, (int)sizeof(IpBuf));
        P = IpBuf;
        while (*P && N < 78) {
            Msg[N++] = *P++;
        }
        if (N < 79) {
            Msg[N++] = '\n';
        }
        Msg[N] = 0;
        ConsoleWriteToJobShell(Msg);
        DebugWrite("lwip: dhcp ok ip=");
        DebugWrite(IpBuf);
        DebugWrite("\n");
    } else {
        ConsoleWriteToJobShell("lwip dhcp: no offer\n");
        DebugWrite("lwip: dhcp timeout\n");
    }
    ConsoleJobReleasePrompt();
}

int LwIpDhcpRestart(int TimeoutMs) {
    if (gJob == DHCP_JOB_RUN) {
        LwIpDhcpStop();
        gJob = DHCP_JOB_IDLE;
    }
    return LwIpDhcpEnqueue(TimeoutMs);
}

int LwIpDhcpEnqueue(int TimeoutMs) {
    struct netif *Netif;
    err_t Err;
    UINT32 Ms;

    if (gJob == DHCP_JOB_RUN) {
        return -1;
    }
    if (!HalNetReady()) {
        return -2;
    }
    if (!LwIpActive() && LwIpInit() != 0) {
        return -2;
    }
    Netif = ToyNetifGet();
    if (!Netif) {
        return -2;
    }
    LwIpDhcpStop();
    ClearNetifAddr(Netif);
    Err = dhcp_start(Netif);
    if (Err != ERR_OK) {
        DebugWrite("lwip: dhcp_start fail\n");
        (void)LwIpApplyConfig();
        return -2;
    }
    gDhcpRunning = 1;
    Ms = TimeoutMs > 0 ? (UINT32)TimeoutMs : 8000u;
    gBudget = Ms * 200u;
    if (gBudget < 20000u) {
        gBudget = 20000u;
    }
#if defined(__x86_64__)
    /* 真机一拍远慢于 5µs。用 TSC，按约 3GHz 把 TimeoutMs 当成墙钟。 */
    gDhcpT0 = DhcpTsc();
    gDhcpNeed = (UINT64)Ms * 3000ULL * 1000ULL;
#endif
    gJob = DHCP_JOB_RUN;
    return 0;
}

/*
 * Worker 泵：0=仍忙；1=空闲或本拍刚结束（已打完成行）。
 */
int LwIpDhcpStep(void) {
    struct netif *Netif;

    if (gJob != DHCP_JOB_RUN) {
        return 1;
    }
    Netif = ToyNetifGet();
    if (!Netif) {
        LwIpDhcpStop();
        gJob = DHCP_JOB_FAIL;
        FinishPrint(0);
        gJob = DHCP_JOB_IDLE;
        return 1;
    }
    LwIpService();
    if (dhcp_supplied_address(Netif)) {
        AdoptFromNetif(Netif);
        gJob = DHCP_JOB_OK;
        FinishPrint(1);
        gJob = DHCP_JOB_IDLE;
        return 1;
    }
#if defined(__x86_64__)
    if (DhcpTsc() - gDhcpT0 >= gDhcpNeed) {
#else
    if (gBudget-- == 0) {
#endif
        LwIpDhcpStop();
        (void)LwIpApplyConfig();
        gJob = DHCP_JOB_FAIL;
        FinishPrint(0);
        gJob = DHCP_JOB_IDLE;
        return 1;
    }
    return 0;
}

/* 遗留名：等同 Enqueue（勿在 Shell 里自旋等待） */
int LwIpDhcpStart(int TimeoutMs) {
    return LwIpDhcpEnqueue(TimeoutMs) == 0 ? 0 : -1;
}

#else

int LwIpDhcpEnqueue(int TimeoutMs) {
    (void)TimeoutMs;
    return -1;
}

int LwIpDhcpRestart(int TimeoutMs) {
    (void)TimeoutMs;
    return -1;
}

int LwIpDhcpStep(void) {
    return 1;
}

int LwIpDhcpJobBusy(void) {
    return 0;
}

int LwIpDhcpStart(int TimeoutMs) {
    (void)TimeoutMs;
    return -1;
}

void LwIpDhcpStop(void) {
}

int LwIpDhcpRunning(void) {
    return 0;
}

#endif
