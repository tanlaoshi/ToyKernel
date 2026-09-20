/*
 * VirtioNet.c — virtio-net MMIO 探测与驱动注册（PR-S-virtionet-virt-1）
 *
 * QEMU：-device virtio-net-device,netdev=n0 -netdev user,id=n0
 * 默认 IP 10.0.2.15；网关 10.0.2.2。ARP/ICMP 留在本 Arch HAL。
 * PR-D3：经 Driver Net 类注册，HalDevices 只见 HalNet*。
 */
#include "VirtioNet.h"
#include "VirtioNetPrivate.h"
#include "VirtioMmio.h"
#include "PhysicalMemory.h"
#include "HalSerial.h"
#include "Hal.h"
#include "Udp.h"
#include "Tcp.h"
#include "Driver.h"
#include "DriverNet.h"
#include "ToySerialLog.h"
#ifdef TOY_LWIP
#include "toy_netif.h"
#endif

VIRTIO_MMIO_DEV gRx;
VIRTIO_MMIO_DEV gTx;
int gNetOk;
UINT8 gMac[6];
UINT32 gIp = HAL_NET_IP_DEFAULT;
UINT32 gNetHdrLen = VIRTIO_NET_HDR_LEGACY;
ARP_ENTRY gArpCache[ARP_CACHE_SIZE];
UINT8 *gRxBuf[RX_BUF_COUNT];
UINT8 *gTxBuf;
UINT16 gPingSeq;
int gPingWait;
UINT32 gPingTarget;
UINT16 gPingId;
UINT32 gTxDone;
UINT32 gRxFrames;
int gLwIpRx;
int gTxBusy;

static void NetFindCb(UINT64 Base, UINT32 DeviceId, void *Ctx) {
    UINT64 *Out = (UINT64 *)Ctx;
    if (DeviceId == VIRTIO_DEV_NET && *Out == 0) {
        *Out = Base;
    }
}

static int VirtioNetDriverProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPrivate) {
    UINT64 Base = 0;
    UINT32 Ver;
    volatile VIRTIO_NET_CFG *Cfg;
    UINT32 i;
    UINT8 *Page;

    (void)Self;
    (void)BusCtx;
    if (gNetOk) {
        if (OutPrivate) {
            *OutPrivate = 0;
        }
        return 0;
    }
    gLwIpRx = 0;
    VirtioMmioScan(NetFindCb, &Base);
    if (Base == 0) {
        return -1;
    }

    if (VirtioMmioNegotiate(&gRx, Base, VIRTIO_DEV_NET, VIRTIO_NET_F_MAC) != 0) {
        ToyLogNet("Boot: VirtIO-Net Negotiate Failed\n");
        return -1;
    }
    Ver = VirtioMmioRead32(Base, 0x004u);
    gNetHdrLen = (Ver >= 2) ? VIRTIO_NET_HDR_V1 : VIRTIO_NET_HDR_LEGACY;

    MemSet(&gTx, 0, sizeof(gTx));
    gTx.Base = Base;
    gTx.DeviceId = VIRTIO_DEV_NET;

    if (VirtioMmioSetupOneQueue(&gRx, RX_QUEUE_ID, RX_BUF_COUNT) != 0) {
        ToyLogNet("Boot: VirtIO-Net RX Queue Failed\n");
        return -1;
    }
    if (VirtioMmioSetupOneQueue(&gTx, TX_QUEUE_ID, 4) != 0) {
        ToyLogNet("Boot: VirtIO-Net TX Queue Failed\n");
        return -1;
    }

    Page = (UINT8 *)PhysicalMemoryAllocatePages(RX_BUF_COUNT + 1);
    if (!Page) {
        return -1;
    }
    for (i = 0; i < RX_BUF_COUNT; i++) {
        gRxBuf[i] = Page + (UINTN)i * PAGE_SIZE;
    }
    gTxBuf = Page + (UINTN)RX_BUF_COUNT * PAGE_SIZE;

    /* DRIVER_OK 后再挂 RX，避免设备过早消费空环 */
    VirtioMmioDriverOk(&gRx);
    RxRefillAll();

    Cfg = (volatile VIRTIO_NET_CFG *)(UINTN)(Base + VIRTIO_NET_CFG_OFF);
    for (i = 0; i < 6; i++) {
        gMac[i] = Cfg->Mac[i];
    }

    gNetOk = 1;
    ToyLogNet("Boot: VirtIO-Net\n");
    if (OutPrivate) {
        *OutPrivate = 0;
    }
    return 0;
}

static void VirtioNetDriverRemove(TOY_DRIVER_INSTANCE *Inst);

static int VirtioNetReady(void);
static void VirtioNetGetMac(UINT8 Mac[6]);
static UINT32 VirtioNetGetIp(void);
static void VirtioNetSetIp(UINT32 Ip);
static void VirtioNetGetStats(UINT32 *TxDone, UINT32 *RxFrames);
static UINT16 VirtioNetChecksum(const void *Data, UINTN Len);
static void VirtioNetSetLwIpRx(int Enable);

static const NET_BACKEND gNetBackend = {
    .Ready = VirtioNetReady,
    .Poll = VirtioNetPoll,
    .GetMac = VirtioNetGetMac,
    .GetIp = VirtioNetGetIp,
    .SetIp = VirtioNetSetIp,
    .FormatIp = VirtioNetFormatIp,
    .ParseIp = VirtioNetParseIp,
    .Ping = VirtioNetPing,
    .GetStats = VirtioNetGetStats,
    .SendIp = VirtioNetSendIp,
    .Checksum = VirtioNetChecksum,
    .SetLwIpRx = VirtioNetSetLwIpRx,
};

static int VirtioNetDriverBind(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
    return ToyDriverNetAttach(&gNetBackend);
}

static void VirtioNetDriverRemove(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
    gNetOk = 0;
}

static const TOY_DRIVER gVirtioNetDriver = {
    .Name = "virtio-net",
    .Class = TOY_DRIVER_CLASS_NET,
    .Match = 0,
    .Probe = VirtioNetDriverProbe,
    .Bind = VirtioNetDriverBind,
    .Remove = VirtioNetDriverRemove,
};

void VirtioNetRegister(void) {
    (void)ToyDriverRegister(&gVirtioNetDriver);
}

int VirtioNetInit(void) {
    (void)ToyDriverProbeClass(TOY_DRIVER_CLASS_NET);
    return 0;
}

static int VirtioNetReady(void) {
    return gNetOk;
}

void VirtioNetPoll(void) {
    if (!gNetOk) {
        return;
    }
    VirtioMmioAckInterrupt(&gRx);
    VirtioMmioAckInterrupt(&gTx);
    TxDrain();
    NetProcessRx();
}

static void VirtioNetGetMac(UINT8 Mac[6]) {
    MemCpy(Mac, gMac, 6);
}

static UINT32 VirtioNetGetIp(void) {
    return gIp;
}

static void VirtioNetSetIp(UINT32 Ip) {
    gIp = Ip;
}

static void VirtioNetGetStats(UINT32 *TxDone, UINT32 *RxFrames) {
    if (TxDone) {
        *TxDone = gTxDone;
    }
    if (RxFrames) {
        *RxFrames = gRxFrames;
    }
}

static UINT16 VirtioNetChecksum(const void *Data, UINTN Len) {
    return Sum16((const UINT8 *)Data, Len);
}

static void VirtioNetSetLwIpRx(int Enable) {
    gLwIpRx = Enable ? 1 : 0;
}

