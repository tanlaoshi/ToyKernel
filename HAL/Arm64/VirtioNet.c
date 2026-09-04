/*
 * VirtioNet.c — virtio-net MMIO + ARP/ICMP（PR-N9）
 *
 * QEMU：-device virtio-net-device,netdev=n0 -netdev user,id=n0
 * 默认 IP 10.0.2.15；网关 10.0.2.2。ARP/ICMP 留在本 Arch HAL。
 */
#include "VirtioNet.h"
#include "VirtioMmio.h"
#include "PhysicalMemory.h"
#include "HalSerial.h"
#include "Hal.h"
#include "Udp.h"
#include "Tcp.h"
#ifdef TOY_LWIP
#include "toy_netif.h"
#endif

#define VIRTIO_NET_F_MAC   (1ULL << 5)
#define VIRTIO_NET_CFG_OFF 0x100u

#define VIRTIO_NET_HDR_LEGACY 10u
#define VIRTIO_NET_HDR_V1     12u

#define ETH_HDR_LEN    14u
#define ETH_MIN_FRAME  60u
#define IP_HDR_LEN     20u
#define ICMP_HDR_LEN   8u

#define ETH_TYPE_ARP   0x0806u
#define ETH_TYPE_IP    0x0800u
#define ARP_OP_REQUEST 1u
#define ARP_OP_REPLY   2u
#define IP_PROTO_ICMP  1u
#define IP_PROTO_UDP   17u
#define IP_PROTO_TCP   6u
#define ICMP_ECHO_REQ  8u
#define ICMP_ECHO_REP  0u

#define RX_QUEUE_ID  0u
#define TX_QUEUE_ID  1u
#define RX_BUF_COUNT 8u
#define RX_BUF_SIZE  2048u
#define ARP_CACHE_SIZE 4u
#define PING_PAYLOAD 32u

typedef struct {
    UINT8 Mac[6];
    UINT16 Status;
} __attribute__((packed)) VIRTIO_NET_CFG;

typedef struct {
    UINT8 Dst[6];
    UINT8 Src[6];
    UINT16 EtherType;
} __attribute__((packed)) ETH_HDR;

typedef struct {
    UINT16 HwType;
    UINT16 ProtoType;
    UINT8 HwLen;
    UINT8 ProtoLen;
    UINT16 Op;
    UINT8 SenderMac[6];
    UINT32 SenderIp;
    UINT8 TargetMac[6];
    UINT32 TargetIp;
} __attribute__((packed)) ARP_PKT;

typedef struct {
    UINT8 VerIhl;
    UINT8 Tos;
    UINT16 TotalLen;
    UINT16 Id;
    UINT16 Frag;
    UINT8 Ttl;
    UINT8 Proto;
    UINT16 Checksum;
    UINT32 Src;
    UINT32 Dst;
} __attribute__((packed)) IP_HDR;

typedef struct {
    UINT8 Type;
    UINT8 Code;
    UINT16 Checksum;
    UINT16 Id;
    UINT16 Seq;
} __attribute__((packed)) ICMP_HDR;

typedef struct {
    UINT32 Ip;
    UINT8 Mac[6];
    int Valid;
} ARP_ENTRY;

static VIRTIO_MMIO_DEV gRx;
static VIRTIO_MMIO_DEV gTx;
static int gNetOk;
static UINT8 gMac[6];
static UINT32 gIp = HAL_NET_IP_DEFAULT;
static UINT32 gNetHdrLen = VIRTIO_NET_HDR_LEGACY;
static ARP_ENTRY gArpCache[ARP_CACHE_SIZE];
static UINT8 *gRxBuf[RX_BUF_COUNT];
static UINT8 *gTxBuf;
static UINT16 gPingSeq;
static int gPingWait;
static UINT32 gPingTarget;
static UINT16 gPingId;
static UINT32 gTxDone;
static UINT32 gRxFrames;
static int gLwIpRx;
static int gTxBusy;

static void MemSet(void *Dst, UINT8 Val, UINTN Len) {
    UINT8 *P = (UINT8 *)Dst;
    UINTN i;
    for (i = 0; i < Len; i++) {
        P[i] = Val;
    }
}

static void MemCpy(void *Dst, const void *Src, UINTN Len) {
    UINT8 *D = (UINT8 *)Dst;
    const UINT8 *S = (const UINT8 *)Src;
    UINTN i;
    for (i = 0; i < Len; i++) {
        D[i] = S[i];
    }
}

static UINT16 ByteSwap16(UINT16 V) {
    return (UINT16)((V << 8) | (V >> 8));
}

static UINT32 ByteSwap32(UINT32 V) {
    return ((V & 0x000000FFu) << 24) | ((V & 0x0000FF00u) << 8) |
           ((V & 0x00FF0000u) >> 8) | ((V & 0xFF000000u) >> 24);
}

static UINT16 Sum16(const UINT8 *Data, UINTN Len) {
    UINT32 Sum = 0;
    UINTN i;
    for (i = 0; i + 1 < Len; i += 2) {
        Sum += ((UINT32)Data[i] << 8) | Data[i + 1];
    }
    if (i < Len) {
        Sum += (UINT32)Data[i] << 8;
    }
    while (Sum >> 16) {
        Sum = (Sum & 0xFFFFu) + (Sum >> 16);
    }
    return (UINT16)~Sum;
}

static void NetFindCb(UINT64 Base, UINT32 DeviceId, void *Ctx) {
    UINT64 *Out = (UINT64 *)Ctx;
    if (DeviceId == VIRTIO_DEV_NET && *Out == 0) {
        *Out = Base;
    }
}

static void RxRefillOne(UINT16 DescId, UINT8 *Buf) {
    UINT16 A;

    gRx.Desc[DescId].Addr = (UINT64)(UINTN)Buf;
    gRx.Desc[DescId].Len = RX_BUF_SIZE;
    gRx.Desc[DescId].Flags = VRING_DESC_F_WRITE;
    gRx.Desc[DescId].Next = 0;
    A = gRx.NextAvail;
    gRx.AvailRing[A % gRx.QueueSize] = DescId;
    __sync_synchronize();
    *gRx.AvailIdx = (UINT16)(A + 1);
    gRx.NextAvail = (UINT16)(A + 1);
}

static void RxRefillAll(void) {
    UINT16 i;
    UINT16 N = gRx.QueueSize;
    if (N > RX_BUF_COUNT) {
        N = RX_BUF_COUNT;
    }
    for (i = 0; i < N; i++) {
        RxRefillOne(i, gRxBuf[i]);
    }
    VirtioMmioNotifyQueue(&gRx, RX_QUEUE_ID);
}

static int ArpLookup(UINT32 Ip, UINT8 Mac[6]) {
    UINT32 i;
    for (i = 0; i < ARP_CACHE_SIZE; i++) {
        if (gArpCache[i].Valid && gArpCache[i].Ip == Ip) {
            MemCpy(Mac, gArpCache[i].Mac, 6);
            return 1;
        }
    }
    return 0;
}

static void ArpLearn(UINT32 Ip, const UINT8 Mac[6]) {
    UINT32 i;
    UINT32 Slot = 0;
    for (i = 0; i < ARP_CACHE_SIZE; i++) {
        if (gArpCache[i].Valid && gArpCache[i].Ip == Ip) {
            MemCpy(gArpCache[i].Mac, Mac, 6);
            return;
        }
        if (!gArpCache[i].Valid) {
            Slot = i;
            break;
        }
    }
    gArpCache[Slot].Ip = Ip;
    MemCpy(gArpCache[Slot].Mac, Mac, 6);
    gArpCache[Slot].Valid = 1;
}

static void TxDrain(void) {
    UINT16 Used = *gTx.UsedIdx;
    while (gTx.LastUsed != Used) {
        gTx.LastUsed = (UINT16)(gTx.LastUsed + 1);
        gTxDone++;
        gTxBusy = 0;
    }
}

static int NetSendFrame(const UINT8 *Frame, UINTN FrameLen) {
    UINTN WireLen;
    UINT16 A;
    int Wait;

    if (!gNetOk || !gTxBuf || FrameLen + gNetHdrLen > RX_BUF_SIZE) {
        return -1;
    }
    if (gTx.QueueSize < 2) {
        return -1;
    }
    WireLen = FrameLen < ETH_MIN_FRAME ? ETH_MIN_FRAME : FrameLen;

    Wait = 100000;
    while (gTxBusy && Wait-- > 0) {
        TxDrain();
        VirtioMmioAckInterrupt(&gTx);
    }
    if (gTxBusy) {
        return -1;
    }

    /* hdr + payload 两段描述符（无 ANY_LAYOUT 时 QEMU 更稳） */
    MemSet(gTxBuf, 0, gNetHdrLen + WireLen);
    MemCpy(gTxBuf + gNetHdrLen, Frame, FrameLen);
    gTx.Desc[0].Addr = (UINT64)(UINTN)gTxBuf;
    gTx.Desc[0].Len = gNetHdrLen;
    gTx.Desc[0].Flags = VRING_DESC_F_NEXT;
    gTx.Desc[0].Next = 1;
    gTx.Desc[1].Addr = (UINT64)(UINTN)(gTxBuf + gNetHdrLen);
    gTx.Desc[1].Len = (UINT32)WireLen;
    gTx.Desc[1].Flags = 0;
    gTx.Desc[1].Next = 0;
    A = gTx.NextAvail;
    gTx.AvailRing[A % gTx.QueueSize] = 0;
    __sync_synchronize();
    *gTx.AvailIdx = (UINT16)(A + 1);
    gTx.NextAvail = (UINT16)(A + 1);
    gTxBusy = 1;
    VirtioMmioNotifyQueue(&gTx, TX_QUEUE_ID);
    return 0;
}

static void NetSendArpRequest(UINT32 TargetIp) {
    UINT8 Frame[ETH_HDR_LEN + sizeof(ARP_PKT)];
    ETH_HDR *Eth = (ETH_HDR *)Frame;
    ARP_PKT *Arp = (ARP_PKT *)(Frame + ETH_HDR_LEN);

    MemSet(Eth->Dst, 0xFF, 6);
    MemCpy(Eth->Src, gMac, 6);
    Eth->EtherType = ByteSwap16(ETH_TYPE_ARP);
    Arp->HwType = ByteSwap16(1);
    Arp->ProtoType = ByteSwap16(ETH_TYPE_IP);
    Arp->HwLen = 6;
    Arp->ProtoLen = 4;
    Arp->Op = ByteSwap16(ARP_OP_REQUEST);
    MemCpy(Arp->SenderMac, gMac, 6);
    Arp->SenderIp = ByteSwap32(gIp);
    MemSet(Arp->TargetMac, 0, 6);
    Arp->TargetIp = ByteSwap32(TargetIp);
    NetSendFrame(Frame, sizeof(Frame));
}

static void HandleArp(const ARP_PKT *Arp) {
    UINT16 Op = ByteSwap16(Arp->Op);
    UINT32 SenderIp = ByteSwap32(Arp->SenderIp);
    if (Op == ARP_OP_REPLY || Op == ARP_OP_REQUEST) {
        ArpLearn(SenderIp, Arp->SenderMac);
    }
    if (Op == ARP_OP_REQUEST && ByteSwap32(Arp->TargetIp) == gIp) {
        UINT8 Frame[ETH_HDR_LEN + sizeof(ARP_PKT)];
        ETH_HDR *Eth = (ETH_HDR *)Frame;
        ARP_PKT *Rep = (ARP_PKT *)(Frame + ETH_HDR_LEN);
        MemCpy(Eth->Dst, Arp->SenderMac, 6);
        MemCpy(Eth->Src, gMac, 6);
        Eth->EtherType = ByteSwap16(ETH_TYPE_ARP);
        Rep->HwType = ByteSwap16(1);
        Rep->ProtoType = ByteSwap16(ETH_TYPE_IP);
        Rep->HwLen = 6;
        Rep->ProtoLen = 4;
        Rep->Op = ByteSwap16(ARP_OP_REPLY);
        MemCpy(Rep->SenderMac, gMac, 6);
        Rep->SenderIp = ByteSwap32(gIp);
        MemCpy(Rep->TargetMac, Arp->SenderMac, 6);
        Rep->TargetIp = Arp->SenderIp;
        NetSendFrame(Frame, sizeof(Frame));
    }
}

static void HandleIcmp(const IP_HDR *Ip, const UINT8 *Payload, UINTN PayloadLen) {
    const ICMP_HDR *Icmp;
    UINT32 Src;

    if (PayloadLen < ICMP_HDR_LEN) {
        return;
    }
    Icmp = (const ICMP_HDR *)Payload;
    if (Icmp->Type != ICMP_ECHO_REP) {
        return;
    }
    Src = ByteSwap32(Ip->Src);
    if (gPingWait && Src == gPingTarget && ByteSwap16(Icmp->Id) == gPingId) {
        gPingWait = 0;
    }
}

static void HandleIpPacket(const UINT8 *Pkt, UINTN Len) {
    const ETH_HDR *Eth;
    const IP_HDR *Ip;
    UINTN IpLen;
    UINTN IpHdrLen;

    gRxFrames++;
    if (Len < ETH_HDR_LEN) {
        return;
    }
    Eth = (const ETH_HDR *)Pkt;
    if (ByteSwap16(Eth->EtherType) == ETH_TYPE_ARP) {
        if (Len >= ETH_HDR_LEN + sizeof(ARP_PKT)) {
            HandleArp((const ARP_PKT *)(Pkt + ETH_HDR_LEN));
        }
        return;
    }
    if (ByteSwap16(Eth->EtherType) != ETH_TYPE_IP ||
        Len < ETH_HDR_LEN + IP_HDR_LEN) {
        return;
    }
    Ip = (const IP_HDR *)(Pkt + ETH_HDR_LEN);
    IpHdrLen = (Ip->VerIhl & 0x0Fu) * 4u;
    if (IpHdrLen < IP_HDR_LEN || Len < ETH_HDR_LEN + IpHdrLen) {
        return;
    }
    IpLen = ByteSwap16(Ip->TotalLen);
    if (IpLen < IpHdrLen || ETH_HDR_LEN + IpLen > Len) {
        return;
    }
    if (ByteSwap32(Ip->Dst) != gIp) {
        return;
    }
    ArpLearn(ByteSwap32(Ip->Src), Eth->Src);
    if (Ip->Proto == IP_PROTO_ICMP) {
        HandleIcmp(Ip, Pkt + ETH_HDR_LEN + IpHdrLen, IpLen - IpHdrLen);
    } else if (Ip->Proto == IP_PROTO_UDP) {
        UdpInput(ByteSwap32(Ip->Src), ByteSwap32(Ip->Dst),
                 Pkt + ETH_HDR_LEN + IpHdrLen, IpLen - IpHdrLen);
    } else if (Ip->Proto == IP_PROTO_TCP) {
        TcpInput(ByteSwap32(Ip->Src), ByteSwap32(Ip->Dst),
                 Pkt + ETH_HDR_LEN + IpHdrLen, IpLen - IpHdrLen);
    }
}

static void NetProcessRx(void) {
    UINT16 Used = *gRx.UsedIdx;

    while (gRx.LastUsed != Used) {
        UINT16 Id = (UINT16)gRx.UsedRing[gRx.LastUsed % gRx.QueueSize].Id;
        UINT32 Len = gRx.UsedRing[gRx.LastUsed % gRx.QueueSize].Len;
        UINT8 *Buf = (UINT8 *)(UINTN)gRx.Desc[Id].Addr;

        if (Buf && Len > gNetHdrLen + ETH_HDR_LEN) {
            const UINT8 *Pkt = Buf + gNetHdrLen;
            UINTN PktLen = Len - gNetHdrLen;
#ifdef TOY_LWIP
            if (gLwIpRx) {
                ToyNetifInput(Pkt, PktLen);
            } else
#endif
            {
                HandleIpPacket(Pkt, PktLen);
            }
        }
        if (Id < RX_BUF_COUNT && gRxBuf[Id]) {
            RxRefillOne(Id, gRxBuf[Id]);
        }
        gRx.LastUsed = (UINT16)(gRx.LastUsed + 1);
    }
    VirtioMmioNotifyQueue(&gRx, RX_QUEUE_ID);
}

static int NetResolve(UINT32 TargetIp, UINT8 Mac[6], int TimeoutMs) {
    int Tries = TimeoutMs > 0 ? TimeoutMs / 10 : 100;
    if (ArpLookup(TargetIp, Mac)) {
        return 0;
    }
    NetSendArpRequest(TargetIp);
    while (Tries-- > 0) {
        VirtioNetPoll();
        if (ArpLookup(TargetIp, Mac)) {
            return 0;
        }
        HalCpuHalt();
    }
    return -1;
}

int VirtioNetInit(void) {
    UINT64 Base = 0;
    UINT32 Ver;
    volatile VIRTIO_NET_CFG *Cfg;
    UINT32 i;
    UINT8 *Page;

    if (gNetOk) {
        return 0;
    }
    gLwIpRx = 0;
    VirtioMmioScan(NetFindCb, &Base);
    if (Base == 0) {
        return 0;
    }

    if (VirtioMmioNegotiate(&gRx, Base, VIRTIO_DEV_NET, VIRTIO_NET_F_MAC) != 0) {
        HalSerialWrite("boot: virtio-net negotiate failed\n");
        return 0;
    }
    Ver = VirtioMmioRead32(Base, 0x004u);
    gNetHdrLen = (Ver >= 2) ? VIRTIO_NET_HDR_V1 : VIRTIO_NET_HDR_LEGACY;

    MemSet(&gTx, 0, sizeof(gTx));
    gTx.Base = Base;
    gTx.DeviceId = VIRTIO_DEV_NET;

    if (VirtioMmioSetupOneQueue(&gRx, RX_QUEUE_ID, RX_BUF_COUNT) != 0) {
        HalSerialWrite("boot: virtio-net rx queue failed\n");
        return 0;
    }
    if (VirtioMmioSetupOneQueue(&gTx, TX_QUEUE_ID, 4) != 0) {
        HalSerialWrite("boot: virtio-net tx queue failed\n");
        return 0;
    }

    Page = (UINT8 *)PhysicalMemoryAllocatePages(RX_BUF_COUNT + 1);
    if (!Page) {
        return 0;
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
    HalSerialWrite("boot: virtio-net\n");
    return 0;
}

int VirtioNetReady(void) {
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

void VirtioNetGetMac(UINT8 Mac[6]) {
    MemCpy(Mac, gMac, 6);
}

UINT32 VirtioNetGetIp(void) {
    return gIp;
}

void VirtioNetFormatIp(UINT32 Ip, char *Buf, int BufLen) {
    UINT8 B[4];
    int Pos = 0;
    int P;

    if (!Buf || BufLen < 16) {
        return;
    }
    B[0] = (UINT8)((Ip >> 24) & 0xFF);
    B[1] = (UINT8)((Ip >> 16) & 0xFF);
    B[2] = (UINT8)((Ip >> 8) & 0xFF);
    B[3] = (UINT8)(Ip & 0xFF);
    for (P = 0; P < 4; P++) {
        UINT8 V = B[P];
        if (V >= 100) {
            Buf[Pos++] = (char)('0' + V / 100);
            V = (UINT8)(V % 100);
            Buf[Pos++] = (char)('0' + V / 10);
            Buf[Pos++] = (char)('0' + V % 10);
        } else if (V >= 10) {
            Buf[Pos++] = (char)('0' + V / 10);
            Buf[Pos++] = (char)('0' + V % 10);
        } else {
            Buf[Pos++] = (char)('0' + V);
        }
        if (P < 3) {
            Buf[Pos++] = '.';
        }
    }
    Buf[Pos] = 0;
}

int VirtioNetParseIp(const char *Text, UINT32 *Ip) {
    UINT32 Parts[4];
    int Part = 0;
    UINT32 Val = 0;
    int Digits = 0;

    if (Text == 0 || Ip == 0) {
        return -1;
    }
    while (*Text) {
        if (*Text >= '0' && *Text <= '9') {
            Val = Val * 10u + (UINT32)(*Text - '0');
            if (Val > 255) {
                return -1;
            }
            Digits++;
        } else if (*Text == '.') {
            if (Digits == 0 || Part >= 3) {
                return -1;
            }
            Parts[Part++] = Val;
            Val = 0;
            Digits = 0;
        } else {
            return -1;
        }
        Text++;
    }
    if (Digits == 0 || Part != 3) {
        return -1;
    }
    Parts[3] = Val;
    *Ip = (Parts[0] << 24) | (Parts[1] << 16) | (Parts[2] << 8) | Parts[3];
    return 0;
}

void VirtioNetGetStats(UINT32 *TxDone, UINT32 *RxFrames) {
    if (TxDone) {
        *TxDone = gTxDone;
    }
    if (RxFrames) {
        *RxFrames = gRxFrames;
    }
}

int VirtioNetSendIp(UINT32 DstIp, UINT8 Proto, const void *Payload,
                    UINTN PayloadLen) {
    UINT8 DstMac[6];
    UINT8 Frame[ETH_HDR_LEN + IP_HDR_LEN + 1400];
    ETH_HDR *Eth;
    IP_HDR *Ip;
    UINT16 IpTotal;

    if (!gNetOk || Payload == 0 || PayloadLen > 1400) {
        return -1;
    }
    if (gLwIpRx) {
        return -1;
    }
    if (NetResolve(DstIp, DstMac, 2000) != 0) {
        return -2;
    }
    MemSet(Frame, 0, sizeof(Frame));
    Eth = (ETH_HDR *)Frame;
    MemCpy(Eth->Dst, DstMac, 6);
    MemCpy(Eth->Src, gMac, 6);
    Eth->EtherType = ByteSwap16(ETH_TYPE_IP);
    Ip = (IP_HDR *)(Frame + ETH_HDR_LEN);
    Ip->VerIhl = 0x45;
    Ip->Ttl = 64;
    Ip->Proto = Proto;
    Ip->Src = ByteSwap32(gIp);
    Ip->Dst = ByteSwap32(DstIp);
    IpTotal = (UINT16)(IP_HDR_LEN + PayloadLen);
    Ip->TotalLen = ByteSwap16(IpTotal);
    Ip->Id = ByteSwap16(0x4F53);
    Ip->Checksum = 0;
    Ip->Checksum = ByteSwap16(Sum16((UINT8 *)Ip, IP_HDR_LEN));
    MemCpy(Frame + ETH_HDR_LEN + IP_HDR_LEN, Payload, PayloadLen);
    return NetSendFrame(Frame, ETH_HDR_LEN + IpTotal);
}

UINT16 VirtioNetChecksum(const void *Data, UINTN Len) {
    return Sum16((const UINT8 *)Data, Len);
}

void VirtioNetSetLwIpRx(int Enable) {
    gLwIpRx = Enable ? 1 : 0;
}

int VirtioNetPing(const char *Host, int TimeoutMs) {
    UINT32 Target;
    UINT8 DstMac[6];
    UINT8 Frame[ETH_HDR_LEN + IP_HDR_LEN + ICMP_HDR_LEN + PING_PAYLOAD];
    ETH_HDR *Eth;
    IP_HDR *Ip;
    ICMP_HDR *Icmp;
    UINT8 *Payload;
    UINT16 IpTotal;
    int Tries;
    int i;

    if (!gNetOk) {
        return -1;
    }
    if (gLwIpRx) {
        return -1;
    }
    if (VirtioNetParseIp(Host, &Target) != 0) {
        return -1;
    }
    if (NetResolve(Target, DstMac, TimeoutMs) != 0) {
        return -1;
    }
    MemSet(Frame, 0, sizeof(Frame));
    Eth = (ETH_HDR *)Frame;
    MemCpy(Eth->Dst, DstMac, 6);
    MemCpy(Eth->Src, gMac, 6);
    Eth->EtherType = ByteSwap16(ETH_TYPE_IP);
    Ip = (IP_HDR *)(Frame + ETH_HDR_LEN);
    Ip->VerIhl = 0x45;
    Ip->Ttl = 64;
    Ip->Proto = IP_PROTO_ICMP;
    Ip->Src = ByteSwap32(gIp);
    Ip->Dst = ByteSwap32(Target);
    IpTotal = IP_HDR_LEN + ICMP_HDR_LEN + PING_PAYLOAD;
    Ip->TotalLen = ByteSwap16(IpTotal);
    Ip->Id = ByteSwap16(0x1234);
    Ip->Checksum = 0;
    Ip->Checksum = ByteSwap16(Sum16((UINT8 *)Ip, IP_HDR_LEN));
    Icmp = (ICMP_HDR *)(Frame + ETH_HDR_LEN + IP_HDR_LEN);
    gPingId = 0x4F53;
    gPingSeq++;
    Icmp->Type = ICMP_ECHO_REQ;
    Icmp->Code = 0;
    Icmp->Id = ByteSwap16(gPingId);
    Icmp->Seq = ByteSwap16(gPingSeq);
    Payload = Frame + ETH_HDR_LEN + IP_HDR_LEN + ICMP_HDR_LEN;
    for (i = 0; i < (int)PING_PAYLOAD; i++) {
        Payload[i] = (UINT8)i;
    }
    Icmp->Checksum = 0;
    Icmp->Checksum =
        ByteSwap16(Sum16((UINT8 *)Icmp, ICMP_HDR_LEN + PING_PAYLOAD));
    gPingTarget = Target;
    gPingWait = 1;
    if (NetSendFrame(Frame, ETH_HDR_LEN + IpTotal) != 0) {
        gPingWait = 0;
        return -2;
    }
    Tries = TimeoutMs > 0 ? TimeoutMs / 10 : 300;
    while (Tries-- > 0 && gPingWait) {
        VirtioNetPoll();
        HalCpuHalt();
    }
    if (gPingWait) {
        gPingWait = 0;
        return -3;
    }
    return 0;
}
