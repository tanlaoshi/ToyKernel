/*
 * Net.c — ARP/ICMP/ping + 网络对外 API（PR-H-net-split-1）
 *
 * virtio 队列/PCI 见 NetVirtio.c；e1000 L2 见 E1000.c / NetE1000.c。
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

typedef struct {
    UINT8  Dst[6];
    UINT8  Src[6];
    UINT16 EtherType;
} __attribute__((packed)) ETH_HDR;

typedef struct {
    UINT16 HwType;
    UINT16 ProtoType;
    UINT8  HwLen;
    UINT8  ProtoLen;
    UINT16 Op;
    UINT8  SenderMac[6];
    UINT32 SenderIp;
    UINT8  TargetMac[6];
    UINT32 TargetIp;
} __attribute__((packed)) ARP_PKT;

typedef struct {
    UINT8  VerIhl;
    UINT8  Tos;
    UINT16 TotalLen;
    UINT16 Id;
    UINT16 Frag;
    UINT8  Ttl;
    UINT8  Proto;
    UINT16 Checksum;
    UINT32 Src;
    UINT32 Dst;
} __attribute__((packed)) IP_HDR;

typedef struct {
    UINT8  Type;
    UINT8  Code;
    UINT16 Checksum;
    UINT16 Id;
    UINT16 Seq;
} __attribute__((packed)) ICMP_HDR;

typedef struct {
    UINT32 Ip;
    UINT8  Mac[6];
    int    Valid;
} ARP_ENTRY;

int gNetOk;
UINT8 gMac[6];
UINT32 gIp = NET_IP_DEFAULT;
static ARP_ENTRY gArpCache[ARP_CACHE_SIZE];
static UINT16 gPingSeq;
static int gPingWait;
static UINT32 gPingTarget;
static UINT16 gPingId;
UINT32 gTxDone;
UINT32 gRxFrames;
int gLwIpRx;

void NetMemSet(void *Dst, UINT8 Val, UINTN Len) {
    UINT8 *P = (UINT8 *)Dst;
    while (Len--) {
        *P++ = Val;
    }
}

void NetMemCpy(void *Dst, const void *Src, UINTN Len) {
    UINT8 *D = (UINT8 *)Dst;
    const UINT8 *S = (const UINT8 *)Src;
    while (Len--) {
        *D++ = *S++;
    }
}

static UINT16 ByteSwap16(UINT16 V) {
    return (UINT16)((V >> 8) | (V << 8));
}

static UINT32 ByteSwap32(UINT32 V) {
    return ((V & 0xFF) << 24) | ((V & 0xFF00) << 8) |
           ((V >> 8) & 0xFF00) | ((V >> 24) & 0xFF);
}

static UINT16 Sum16(const UINT8 *Data, UINTN Len) {
    UINT32 Sum = 0;
    while (Len > 1) {
        Sum += ((UINT16)Data[0] << 8) | Data[1];
        Data += 2;
        Len -= 2;
    }
    if (Len) {
        Sum += (UINT16)Data[0] << 8;
    }
    while (Sum >> 16) {
        Sum = (Sum & 0xFFFF) + (Sum >> 16);
    }
    return (UINT16)~Sum;
}

static int NetResolve(UINT32 TargetIp, UINT8 Mac[6], int TimeoutMs);
static int NetSendFrame(const UINT8 *Frame, UINTN FrameLen);

static int NetSendFrame(const UINT8 *Frame, UINTN FrameLen) {
    UINT16 Head;
    UINT16 Slot;
    int Wait;
    UINTN WireLen;
    UINT64 IrqFlags;
    int NicResult;

    if (!gNetOk) {
        return -1;
    }
    NicResult = NetNicSendFrame(Frame, FrameLen);
    if (NicResult != -2) {
        return NicResult;
    }
    if (FrameLen + VIRTIO_NET_HDR_LEN > RX_BUF_SIZE) {
        return -1;
    }

    /* Pad to Ethernet minimum so short ARP frames are accepted. */
    WireLen = FrameLen < ETH_MIN_FRAME ? ETH_MIN_FRAME : FrameLen;

    /* 关中断：避免 syscall/Halt 期间被定时器抢到 shell 再进 NetPoll 打坏 vring */
    IrqFlags = HalIrqSave();

    /* Wait for previous TX descriptors to complete (single-buffer TX). */
    Wait = 100000;
    while (gTxQ.NumFree < gTxQ.Size && Wait-- > 0) {
        UINT16 DoneHead;
        UINT32 DoneLen;
        while (VirtQueuePopUsed(&gTxQ, &DoneHead, &DoneLen)) {
            if (DoneHead < gTxQ.Size) {
                VirtQueueFreeDescriptor(&gTxQ, DoneHead);
                gTxDone++;
            }
            (void)DoneLen;
        }
    }

    Head = VirtQueueAllocateDescriptor(&gTxQ);
    if (Head == (UINT16)~0 || Head >= gTxQ.Size) {
        HalIrqRestore(IrqFlags);
        return -1;
    }
    NetMemSet(gTxBuf, 0, VIRTIO_NET_HDR_LEN + WireLen);
    NetMemCpy(gTxBuf + VIRTIO_NET_HDR_LEN, Frame, FrameLen);
    gTxQ.Desc[Head].Addr = VirtualToPhysical(gTxBuf);
    gTxQ.Desc[Head].Len = (UINT32)(WireLen + VIRTIO_NET_HDR_LEN);
    gTxQ.Desc[Head].Flags = 0;
    gTxQ.Desc[Head].Next = 0;
    Slot = gTxQ.AvailIdx % gTxQ.Size;
    gTxQ.Avail->Ring[Slot] = Head;
    __asm__ volatile("mfence" ::: "memory");
    gTxQ.AvailIdx++;
    gTxQ.Avail->Idx = gTxQ.AvailIdx;
    VirtQueueKick(&gTxQ, TX_QUEUE_ID);
    HalIrqRestore(IrqFlags);
    return 0;
}

int NetSendEthernet(const UINT8 *Frame, UINTN Len) {
    if (Frame == 0 || Len < ETH_HDR_LEN) {
        return -1;
    }
    return NetSendFrame(Frame, Len);
}

void NetSetLwIpRx(int Enable) {
    gLwIpRx = Enable ? 1 : 0;
}

int NetLwIpRx(void) {
    return gLwIpRx;
}

static int ArpLookup(UINT32 Ip, UINT8 Mac[6]) {
    int i;
    for (i = 0; i < ARP_CACHE_SIZE; i++) {
        if (gArpCache[i].Valid && gArpCache[i].Ip == Ip) {
            NetMemCpy(Mac, gArpCache[i].Mac, 6);
            return 1;
        }
    }
    return 0;
}

static void ArpLearn(UINT32 Ip, const UINT8 Mac[6]) {
    int i;
    int Slot = 0;
    for (i = 0; i < ARP_CACHE_SIZE; i++) {
        if (gArpCache[i].Valid && gArpCache[i].Ip == Ip) {
            NetMemCpy(gArpCache[i].Mac, Mac, 6);
            return;
        }
        if (!gArpCache[i].Valid) {
            Slot = i;
            break;
        }
    }
    gArpCache[Slot].Ip = Ip;
    NetMemCpy(gArpCache[Slot].Mac, Mac, 6);
    gArpCache[Slot].Valid = 1;
}

static void NetSendArpRequest(UINT32 TargetIp) {
    UINT8 Frame[ETH_HDR_LEN + sizeof(ARP_PKT)];
    ETH_HDR *Eth = (ETH_HDR *)Frame;
    ARP_PKT *Arp = (ARP_PKT *)(Frame + ETH_HDR_LEN);

    NetMemSet(Eth->Dst, 0xFF, 6);
    NetMemCpy(Eth->Src, gMac, 6);
    Eth->EtherType = ByteSwap16(ETH_TYPE_ARP);
    Arp->HwType = ByteSwap16(1);
    Arp->ProtoType = ByteSwap16(ETH_TYPE_IP);
    Arp->HwLen = 6;
    Arp->ProtoLen = 4;
    Arp->Op = ByteSwap16(ARP_OP_REQUEST);
    NetMemCpy(Arp->SenderMac, gMac, 6);
    Arp->SenderIp = ByteSwap32(gIp);
    NetMemSet(Arp->TargetMac, 0, 6);
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
        NetMemCpy(Eth->Dst, Arp->SenderMac, 6);
        NetMemCpy(Eth->Src, gMac, 6);
        Eth->EtherType = ByteSwap16(ETH_TYPE_ARP);
        Rep->HwType = ByteSwap16(1);
        Rep->ProtoType = ByteSwap16(ETH_TYPE_IP);
        Rep->HwLen = 6;
        Rep->ProtoLen = 4;
        Rep->Op = ByteSwap16(ARP_OP_REPLY);
        NetMemCpy(Rep->SenderMac, gMac, 6);
        Rep->SenderIp = ByteSwap32(gIp);
        NetMemCpy(Rep->TargetMac, Arp->SenderMac, 6);
        Rep->TargetIp = Arp->SenderIp;
        NetSendFrame(Frame, sizeof(Frame));
    }
}

static void HandleIcmp(const IP_HDR *Ip, const UINT8 *Payload, UINTN PayloadLen) {
    const ICMP_HDR *Icmp;
    UINTN IpHdrLen;
    UINT32 Src;

    if (PayloadLen < ICMP_HDR_LEN) {
        return;
    }
    Icmp = (const ICMP_HDR *)Payload;
    if (Icmp->Type != ICMP_ECHO_REPLY) {
        return;
    }
    IpHdrLen = (Ip->VerIhl & 0x0F) * 4;
    (void)IpHdrLen;
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
    if (ByteSwap16(Eth->EtherType) != ETH_TYPE_IP || Len < ETH_HDR_LEN + IP_HDR_LEN) {
        return;
    }
    Ip = (const IP_HDR *)(Pkt + ETH_HDR_LEN);
    IpHdrLen = (Ip->VerIhl & 0x0F) * 4;
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
        HandleIcmp(Ip, Pkt + ETH_HDR_LEN + IpHdrLen,
                   IpLen - IpHdrLen);
    } else if (Ip->Proto == IP_PROTO_UDP) {
        UdpInput(ByteSwap32(Ip->Src), ByteSwap32(Ip->Dst),
                 Pkt + ETH_HDR_LEN + IpHdrLen, IpLen - IpHdrLen);
    } else if (Ip->Proto == IP_PROTO_TCP) {
        TcpInput(ByteSwap32(Ip->Src), ByteSwap32(Ip->Dst),
                 Pkt + ETH_HDR_LEN + IpHdrLen, IpLen - IpHdrLen);
    }
}

static void NetProcessRx(void) {
    UINT16 Head;
    UINT32 Len;
    UINT8 *Buf;
    UINT64 Phys;
    UINTN i;
    int Ok;

    while (VirtQueuePopUsed(&gRxQ, &Head, &Len)) {
        Phys = gRxQ.Desc[Head].Addr;
        Buf = (UINT8 *)(UINTN)Phys;
        /* Desc.Addr 是物理地址；恒等映射下可当指针，但必须落在 RX 池内 */
        Ok = 0;
        for (i = 0; i < RX_BUF_COUNT; i++) {
            if (Buf == gRxBufData[i]) {
                Ok = 1;
                break;
            }
        }
        if (Ok && Len > VIRTIO_NET_HDR_LEN + ETH_HDR_LEN) {
            const UINT8 *Pkt = Buf + VIRTIO_NET_HDR_LEN;
            UINTN PktLen = Len - VIRTIO_NET_HDR_LEN;
#ifdef TOY_LWIP
            if (gLwIpRx) {
                /* lwip on: all IP RX to lwIP; builtin HandleIpPacket idle */
                ToyNetifInput(Pkt, PktLen);
            } else
#endif
            {
                HandleIpPacket(Pkt, PktLen);
            }
        }
        VirtQueueFreeDescriptor(&gRxQ, Head);
        if (Ok) {
            ReceiveRefillOne(&gRxQ, Buf);
        } else if (Head < RX_BUF_COUNT) {
            ReceiveRefillOne(&gRxQ, gRxBufData[Head]);
        }
    }
}

int NetSendIp(UINT32 DstIp, UINT8 Proto, const void *Payload, UINTN PayloadLen) {
    UINT8 DstMac[6];
    static UINT8 Frame[ETH_HDR_LEN + IP_HDR_LEN + 1400]; /* 任务栈仅 8KiB */
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
    NetMemSet(Frame, 0, sizeof(Frame));
    Eth = (ETH_HDR *)Frame;
    NetMemCpy(Eth->Dst, DstMac, 6);
    NetMemCpy(Eth->Src, gMac, 6);
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
    NetMemCpy(Frame + ETH_HDR_LEN + IP_HDR_LEN, Payload, PayloadLen);
    return NetSendFrame(Frame, ETH_HDR_LEN + IpTotal);
}

UINT16 NetChecksum(const void *Data, UINTN Len) {
    return Sum16((const UINT8 *)Data, Len);
}

int NetInit(void) {
    (void)ToyDriverProbeClass(TOY_DRIVER_CLASS_NET);
    return 0;
}

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

void NetInputFrame(const UINT8 *Pkt, UINTN Len) {
    if (!Pkt || Len < ETH_HDR_LEN) {
        return;
    }
#ifdef TOY_LWIP
    if (gLwIpRx) {
        ToyNetifInput(Pkt, Len);
        return;
    }
#endif
    HandleIpPacket(Pkt, Len);
}

int NetReady(void) {
    return gNetOk;
}

void NetGetStats(UINT32 *TxDone, UINT32 *RxFrames) {
    if (TxDone) {
        *TxDone = gTxDone;
    }
    if (RxFrames) {
        *RxFrames = gRxFrames;
    }
}

void NetPoll(void) {
    UINT16 Head;
    UINT32 Len;
    UINT64 IrqFlags;

    if (!gNetOk) {
        return;
    }
    if (NetNicHasL2()) {
        NetNicPoll();
        return;
    }
    IrqFlags = HalIrqSave();
    if (gIsr) {
        *gIsr = 0;
    }
    while (VirtQueuePopUsed(&gTxQ, &Head, &Len)) {
        if (Head < gTxQ.Size) {
            VirtQueueFreeDescriptor(&gTxQ, Head);
            gTxDone++;
        }
        (void)Len;
    }
    NetProcessRx();
    HalIrqRestore(IrqFlags);
}

void NetGetMac(UINT8 Mac[6]) {
    NetMemCpy(Mac, gMac, 6);
}

UINT32 NetGetIp(void) {
    return gIp;
}

void NetSetIp(UINT32 Ip) {
    gIp = Ip;
}

void NetFormatIp(UINT32 Ip, char *Buf, int BufLen) {
    UINT8 B[4];
    int Pos = 0;
    int P;

    if (BufLen < 16) {
        return;
    }
    B[0] = (UINT8)((Ip >> 24) & 0xFF);
    B[1] = (UINT8)((Ip >> 16) & 0xFF);
    B[2] = (UINT8)((Ip >> 8) & 0xFF);
    B[3] = (UINT8)(Ip & 0xFF);
    for (P = 0; P < 4; P++) {
        UINT8 V = B[P];
        if (V >= 100) {
            Buf[Pos++] = '0' + V / 100;
            V %= 100;
            Buf[Pos++] = '0' + V / 10;
            Buf[Pos++] = '0' + V % 10;
        } else if (V >= 10) {
            Buf[Pos++] = '0' + V / 10;
            Buf[Pos++] = '0' + V % 10;
        } else {
            Buf[Pos++] = '0' + V;
        }
        if (P < 3) {
            Buf[Pos++] = '.';
        }
    }
    Buf[Pos] = 0;
}

int NetParseIp(const char *Text, UINT32 *Ip) {
    UINT32 Parts[4];
    int Part = 0;
    UINT32 Val = 0;
    int Digits = 0;

    if (Text == 0 || Ip == 0) {
        return -1;
    }
    while (*Text) {
        if (*Text >= '0' && *Text <= '9') {
            Val = Val * 10 + (UINT32)(*Text - '0');
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

void NetInfo(void) {
    char IpBuf[20];
    if (!gNetOk) {
        DebugWrite("Net: not available\n");
        return;
    }
    NetFormatIp(gIp, IpBuf, sizeof(IpBuf));
    DebugWrite("Net: mac ");
    for (int i = 0; i < 6; i++) {
        DebugWrite(Uint8ToDecimal(gMac[i]));
        if (i < 5) {
            DebugWrite(":");
        }
    }
    DebugWrite(" ip ");
    DebugWrite(IpBuf);
    DebugWrite(" (QEMU user)\n");
}

static int NetResolve(UINT32 TargetIp, UINT8 Mac[6], int TimeoutMs) {
    int Tries = TimeoutMs > 0 ? TimeoutMs / 10 : 100;
    if (ArpLookup(TargetIp, Mac)) {
        return 0;
    }
    NetSendArpRequest(TargetIp);
    while (Tries-- > 0) {
        NetPoll();
        if (ArpLookup(TargetIp, Mac)) {
            return 0;
        }
    }
    DebugWrite("Net: arp fail");
    {
        UINT32 TxDone = 0;
        UINT32 RxFrames = 0;
        NetGetStats(&TxDone, &RxFrames);
        DebugWrite(" tx=");
        DebugHex32(TxDone);
        DebugWrite(" rx=");
        DebugHex32(RxFrames);
    }
    DebugWrite("\n");
    return -1;
}

int NetPing(const char *Host, int TimeoutMs) {
    UINT32 Target;
    UINT8 DstMac[6];
    UINT8 Frame[ETH_HDR_LEN + IP_HDR_LEN + ICMP_HDR_LEN + PING_PAYLOAD];
    ETH_HDR *Eth;
    IP_HDR *Ip;
    ICMP_HDR *Icmp;
    UINT8 *Payload;
    UINT16 IpTotal;
    int Tries;

    if (!gNetOk) {
        return -1;
    }
    if (gLwIpRx) {
        return -1;
    }
    if (NetParseIp(Host, &Target) != 0) {
        return -1;
    }
    if (NetResolve(Target, DstMac, TimeoutMs) != 0) {
        return -1;
    }
    NetMemSet(Frame, 0, sizeof(Frame));
    Eth = (ETH_HDR *)Frame;
    NetMemCpy(Eth->Dst, DstMac, 6);
    NetMemCpy(Eth->Src, gMac, 6);
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
    Icmp->Type = ICMP_ECHO_REQUEST;
    Icmp->Code = 0;
    Icmp->Id = ByteSwap16(gPingId);
    Icmp->Seq = ByteSwap16(gPingSeq);
    Payload = Frame + ETH_HDR_LEN + IP_HDR_LEN + ICMP_HDR_LEN;
    for (int i = 0; i < PING_PAYLOAD; i++) {
        Payload[i] = (UINT8)i;
    }
    Icmp->Checksum = 0;
    Icmp->Checksum = ByteSwap16(Sum16((UINT8 *)Icmp, ICMP_HDR_LEN + PING_PAYLOAD));
    gPingTarget = Target;
    gPingWait = 1;
    if (NetSendFrame(Frame, ETH_HDR_LEN + IpTotal) != 0) {
        gPingWait = 0;
        return -2;
    }
    Tries = TimeoutMs > 0 ? TimeoutMs / 10 : 300;
    while (Tries-- > 0 && gPingWait) {
        NetPoll();
        HalCpuHalt();
    }
    if (gPingWait) {
        gPingWait = 0;
        return -3;
    }
    return 0;
}
