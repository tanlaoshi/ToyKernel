/*
 * NetDriver.c — virtio-net 驱动注册（PR-S-net-1）
 */
#include "NetPrivate.h"
#include "Udp.h"
#include "Tcp.h"
#ifdef TOY_LWIP
#include "toy_netif.h"
#endif
#include "PhysicalMemory.h"
#include "VirtualMemory.h"
#include "Serial.h"
#include "Debug.h"
#include "Hal.h"
#include "Driver.h"
#include "DriverNet.h"
#include "DriverNic.h"

static int NetDriverProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPrivate) {
    UINT8 Bus;
    UINT8 Dev;
    UINT8 Fn;
    UINT64 Bar;

    (void)Self;
    (void)BusCtx;
    if (gNetOk) {
        if (OutPrivate) {
            *OutPrivate = 0;
        }
        return 0;
    }
    /* BAR Map 必须在 VMM Enable 之后（InitDriver 的 ProbeAll 会跳过） */
    if (!VirtualMemoryEnabled()) {
        return -1;
    }
    gLwIpRx = 0;
    if (!VirtioFindNet(&Bus, &Dev, &Fn, &Bar)) {
        DebugWrite("Net: virtio-net not found\n");
        return -1;
    }
    if (VirtioNetStart(Bus, Dev, Fn, Bar) != 0) {
        DebugWrite("Net: virtio init failed\n");
        return -1;
    }
    gNetOk = 1;
    DebugWrite("Net: virtio-net up\n");
    if (OutPrivate) {
        *OutPrivate = 0;
    }
    return 0;
}

static int NetDriverBind(TOY_DRIVER_INSTANCE *Inst);
static void NetDriverRemove(TOY_DRIVER_INSTANCE *Inst);

/* Net.h 导出的 ops；此处仅作后端表前向声明 */
int NetReady(void);
void NetPoll(void);
void NetGetMac(UINT8 Mac[6]);
UINT32 NetGetIp(void);
void NetFormatIp(UINT32 Ip, char *Buf, int BufLen);
int NetParseIp(const char *Text, UINT32 *Ip);
int NetPing(const char *Host, int TimeoutMs);
void NetGetStats(UINT32 *TxDone, UINT32 *RxFrames);
int NetSendIp(UINT32 DstIp, UINT8 Proto, const void *Payload, UINTN PayloadLen);
UINT16 NetChecksum(const void *Data, UINTN Len);
void NetSetLwIpRx(int Enable);

static const NET_BACKEND gNetBackend = {
    .Ready = NetReady,
    .Poll = NetPoll,
    .GetMac = NetGetMac,
    .GetIp = NetGetIp,
    .SetIp = NetSetIp,
    .FormatIp = NetFormatIp,
    .ParseIp = NetParseIp,
    .Ping = NetPing,
    .GetStats = NetGetStats,
    .SendIp = NetSendIp,
    .Checksum = NetChecksum,
    .SetLwIpRx = NetSetLwIpRx,
};

static int NetDriverBind(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
    return ToyDriverNetAttach(&gNetBackend);
}

static void NetDriverRemove(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
    gNetOk = 0;
}

static const TOY_DRIVER gVirtioNetPciDriver = {
    .Name = "virtio-net-pci",
    .Class = TOY_DRIVER_CLASS_NET,
    .Match = 0,
    .Probe = NetDriverProbe,
    .Bind = NetDriverBind,
    .Remove = NetDriverRemove,
};

void NetDriverRegister(void) {
    (void)ToyDriverRegister(&gVirtioNetPciDriver);
}

int NetProtocolAttach(void) {
    return ToyDriverNetAttach(&gNetBackend);
}
