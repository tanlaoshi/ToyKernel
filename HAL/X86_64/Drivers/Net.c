/*
 * Net.c — virtio-net-pci 轮询驱动 + ARP + ICMP ping
 *
 * QEMU: -device virtio-net-pci,netdev=n0 -netdev user,id=n0
 * 默认 IP 10.0.2.15/24，网关 10.0.2.2
 */
#include "Net.h"
#include "Udp.h"
#include "Tcp.h"
#include "PCIe.h"
#include "PhysicalMemory.h"
#include "VirtualMemory.h"
#include "Serial.h"
#include "Debug.h"
#include "Hal.h"

#define VIRTIO_VENDOR_ID      0x1AF4
#define VIRTIO_DEV_NET        0x1000
#define PCI_CAP_VENDOR        0x09
#define VIRTIO_PCI_CAP_COMMON 1
#define VIRTIO_PCI_CAP_ISR    3
#define VIRTIO_PCI_CAP_DEVICE 4
#define VIRTIO_PCI_CAP_NOTIFY 2

#define VIRTIO_STATUS_ACK       1
#define VIRTIO_STATUS_DRIVER    2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_STATUS_FAILED    128

#define VIRTIO_F_VERSION_1      (1ULL << 32)
#define VIRTIO_NET_F_MAC        (1ULL << 5)

#define VRING_DESC_F_NEXT       1
#define VRING_DESC_F_WRITE      2

/*
 * With VIRTIO_F_VERSION_1, QEMU uses virtio_net_hdr_mrg_rxbuf (12 bytes),
 * even when VIRTIO_NET_F_MRG_RXBUF is not negotiated.
 */
#define VIRTIO_NET_HDR_LEN      12
#define ETH_HDR_LEN             14
#define ETH_MIN_FRAME           60
#define IP_HDR_LEN              20
#define ICMP_HDR_LEN            8

#define ETH_TYPE_ARP            0x0806
#define ETH_TYPE_IP             0x0800
#define ARP_OP_REQUEST          1
#define ARP_OP_REPLY            2
#define IP_PROTO_ICMP           1
#define IP_PROTO_TCP            6
#define IP_PROTO_UDP            17
#define ICMP_ECHO_REQUEST       8
#define ICMP_ECHO_REPLY         0

#define RX_QUEUE_ID             0
#define TX_QUEUE_ID             1
#define RX_BUF_SIZE             2048
#define RX_BUF_COUNT            16
#define ARP_CACHE_SIZE          4
#define PING_PAYLOAD            32

typedef struct {
    UINT64 Addr;
    UINT32 Len;
    UINT16 Flags;
    UINT16 Next;
} __attribute__((packed)) VRING_DESC;

typedef struct {
    UINT16 Flags;
    UINT16 Idx;
    UINT16 Ring[];
} VRING_AVAIL;

typedef struct {
    UINT32 Id;
    UINT32 Len;
} __attribute__((packed)) VRING_USED_ELEM;

typedef struct {
    UINT16 Flags;
    UINT16 Idx;
    VRING_USED_ELEM Ring[];
} VRING_USED;

typedef struct {
    UINT32 DeviceFeatureSelect;
    UINT32 DeviceFeature;
    UINT32 DriverFeatureSelect;
    UINT32 DriverFeature;
    UINT16 MsixConfig;
    UINT16 NumQueues;
    UINT8  DeviceStatus;
    UINT8  ConfigGeneration;
    UINT16 QueueSelect;
    UINT16 QueueSize;
    UINT16 QueueMsixVector;
    UINT16 QueueEnable;
    UINT16 QueueNotifyOff;
    /* offset 32: queue_desc — no padding (virtio-pci modern common cfg) */
    UINT64 QueueDesc;
    UINT64 QueueDriver;
    UINT64 QueueDevice;
} __attribute__((packed)) VIRTIO_COMMON_CFG;

typedef struct {
    UINT8 Mac[6];
    UINT16 Status;
} __attribute__((packed)) VIRTIO_NET_CFG;

typedef struct {
    UINT8  Flags;
    UINT8  GsoType;
    UINT16 HdrLen;
    UINT16 GsoSize;
    UINT16 CsumStart;
    UINT16 CsumOffset;
} __attribute__((packed)) VIRTIO_NET_HDR;

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
    UINT16 Size;
    UINT16 LastUsedIdx;
    UINT16 FreeHead;
    UINT16 NumFree;
    VRING_DESC *Desc;
    VRING_AVAIL *Avail;
    VRING_USED *Used;
    UINT16 AvailIdx;
    UINT16 NotifyOff;
    volatile VIRTIO_COMMON_CFG *Common;
    volatile UINT8 *NotifyBase;
    UINT32 NotifyMult;
} VIRTQ;

static UINT8 gVirtQueueRxMemory[4 * PAGE_SIZE] __attribute__((aligned(4096)));
static UINT8 gVirtQueueTxMemory[4 * PAGE_SIZE] __attribute__((aligned(4096)));
static UINT8 gRxBufData[RX_BUF_COUNT][PAGE_SIZE] __attribute__((aligned(4096)));
static UINT8 gTxBufData[PAGE_SIZE] __attribute__((aligned(4096)));

typedef struct {
    UINT32 Ip;
    UINT8  Mac[6];
    int    Valid;
} ARP_ENTRY;

static volatile VIRTIO_COMMON_CFG *gCommon;
static volatile VIRTIO_NET_CFG *gDevCfg;
static VIRTQ gRxQ;
static VIRTQ gTxQ;
static int gNetOk;
static UINT8 gMac[6];
static UINT32 gIp = NET_IP_DEFAULT;
static ARP_ENTRY gArpCache[ARP_CACHE_SIZE];
static UINT16 gPingSeq;
static int gPingWait;
static UINT32 gPingTarget;
static UINT16 gPingId;
static volatile UINT8 *gIsr;
static UINT16 gIoPort;

static UINT32 gTxDone;
static UINT32 gRxFrames;

void NetGetStats(UINT32 *TxDone, UINT32 *RxFrames);
static int NetResolve(UINT32 TargetIp, UINT8 Mac[6], int TimeoutMs);

static void MemSet(void *Dst, UINT8 Val, UINTN Len) {
    UINT8 *P = (UINT8 *)Dst;
    while (Len--) {
        *P++ = Val;
    }
}

static void MemCpy(void *Dst, const void *Src, UINTN Len) {
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

static UINT64 VirtualToPhysical(void *Ptr) {
    return (UINT64)(UINTN)Ptr;
}

static void VirtQueueKick(VIRTQ *Q, UINT16 QueueId) {
    __asm__ volatile("mfence" ::: "memory");
    if (Q->NotifyBase != 0) {
        volatile UINT16 *Notify = (volatile UINT16 *)(Q->NotifyBase +
                                                        (UINTN)Q->NotifyOff * Q->NotifyMult);
        *Notify = QueueId;
    } else if (gIoPort != 0) {
        HalIoWrite16((UINT16)(gIoPort + 16), QueueId);
    }
    __asm__ volatile("mfence" ::: "memory");
}

static UINTN VirtQueueMemorySize(UINT16 Size) {
    UINTN Desc = Size * sizeof(VRING_DESC);
    UINTN Avail = sizeof(UINT16) * (2 + Size);
    UINTN Used = sizeof(UINT16) * 2 + Size * sizeof(VRING_USED_ELEM);
    UINTN Off = Desc + Avail;
    Off = (Off + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    return Off + Used;
}

static int VirtQueueSetup(VIRTQ *Q, UINT16 QueueId, volatile VIRTIO_COMMON_CFG *Common,
                   volatile UINT8 *NotifyBase, UINT32 NotifyMult) {
    UINT8 *Mem;
    UINTN MemSize;
    UINTN UsedOff;
    int i;

    Q->Common = Common;
    Q->NotifyBase = NotifyBase;
    Q->NotifyMult = NotifyMult;
    Common->QueueSelect = QueueId;
    Q->Size = Common->QueueSize;
    if (Q->Size == 0) {
        return -1;
    }
    MemSize = VirtQueueMemorySize(Q->Size);
    if (QueueId == RX_QUEUE_ID) {
        Mem = gVirtQueueRxMemory;
    } else {
        Mem = gVirtQueueTxMemory;
    }
    if (MemSize > 4 * PAGE_SIZE) {
        return -1;
    }
    MemSet(Mem, 0, MemSize);
    Q->Desc = (VRING_DESC *)Mem;
    Q->Avail = (VRING_AVAIL *)(Mem + Q->Size * sizeof(VRING_DESC));
    UsedOff = ((UINTN)Q->Avail + sizeof(UINT16) * (2 + Q->Size) + PAGE_SIZE - 1) &
              ~(PAGE_SIZE - 1);
    Q->Used = (VRING_USED *)(Mem + UsedOff);
    Q->AvailIdx = 0;
    Q->LastUsedIdx = 0;
    Q->FreeHead = 0;
    Q->NumFree = Q->Size;
    for (i = 0; i < (int)Q->Size - 1; i++) {
        Q->Desc[i].Next = (UINT16)(i + 1);
    }
    Q->NotifyOff = Common->QueueNotifyOff;
    Common->QueueDesc = VirtualToPhysical(Q->Desc);
    Common->QueueDriver = VirtualToPhysical(Q->Avail);
    Common->QueueDevice = VirtualToPhysical(Q->Used);
    __asm__ volatile("" ::: "memory");
    return 0;
}

static void VirtQueueEnable(VIRTQ *Q, UINT16 QueueId) {
    Q->Common->QueueSelect = QueueId;
    Q->NotifyOff = Q->Common->QueueNotifyOff;
    __asm__ volatile("" ::: "memory");
    Q->Common->QueueEnable = 1;
}

static UINT16 VirtQueueAllocateDescriptor(VIRTQ *Q) {
    UINT16 Idx;
    if (Q->NumFree == 0) {
        return (UINT16)~0;
    }
    Idx = Q->FreeHead;
    Q->FreeHead = Q->Desc[Idx].Next;
    Q->NumFree--;
    Q->Desc[Idx].Flags = 0;
    Q->Desc[Idx].Next = 0;
    return Idx;
}

static void VirtQueueFreeDescriptor(VIRTQ *Q, UINT16 Idx) {
    Q->Desc[Idx].Next = Q->FreeHead;
    Q->FreeHead = Idx;
    Q->NumFree++;
}

static void VirtQueueSubmitAvailable(VIRTQ *Q, UINT16 Head) {
    UINT16 Slot = Q->AvailIdx % Q->Size;
    Q->Avail->Ring[Slot] = Head;
    __asm__ volatile("" ::: "memory");
    Q->AvailIdx++;
    Q->Avail->Idx = Q->AvailIdx;
    VirtQueueKick(Q, Q == &gRxQ ? RX_QUEUE_ID : TX_QUEUE_ID);
}

static int VirtQueuePopUsed(VIRTQ *Q, UINT16 *Head, UINT32 *Len) {
    UINT16 UsedIdx;

    __asm__ volatile("" ::: "memory");
    UsedIdx = Q->Used->Idx;
    if (Q->LastUsedIdx == UsedIdx) {
        return 0;
    }
    {
        UINT16 Idx = Q->LastUsedIdx % Q->Size;
        VRING_USED_ELEM *Elem = &Q->Used->Ring[Idx];
        *Head = (UINT16)Elem->Id;
        *Len = Elem->Len;
        Q->LastUsedIdx++;
    }
    return 1;
}

static void ReceiveRefillOne(VIRTQ *Q, UINT8 *Buf) {
    UINT16 Head = VirtQueueAllocateDescriptor(Q);
    if (Head == (UINT16)~0) {
        return;
    }
    Q->Desc[Head].Addr = VirtualToPhysical(Buf);
    Q->Desc[Head].Len = RX_BUF_SIZE;
    Q->Desc[Head].Flags = VRING_DESC_F_WRITE;
    VirtQueueSubmitAvailable(Q, Head);
}

static void ReceiveRefillAll(void) {
    int i;
    for (i = 0; i < RX_BUF_COUNT; i++) {
        ReceiveRefillOne(&gRxQ, gRxBufData[i]);
    }
}

static int ArpLookup(UINT32 Ip, UINT8 Mac[6]) {
    int i;
    for (i = 0; i < ARP_CACHE_SIZE; i++) {
        if (gArpCache[i].Valid && gArpCache[i].Ip == Ip) {
            MemCpy(Mac, gArpCache[i].Mac, 6);
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

static UINT8 *gTxBuf = gTxBufData;

static int NetSendFrame(const UINT8 *Frame, UINTN FrameLen) {
    UINT16 Head;
    UINT16 Slot;
    int Wait;
    UINTN WireLen;

    if (!gNetOk || FrameLen + VIRTIO_NET_HDR_LEN > RX_BUF_SIZE) {
        return -1;
    }

    /* Pad to Ethernet minimum so short ARP frames are accepted. */
    WireLen = FrameLen < ETH_MIN_FRAME ? ETH_MIN_FRAME : FrameLen;

    /* Wait for previous TX descriptors to complete (single-buffer TX). */
    Wait = 100000;
    while (gTxQ.NumFree < gTxQ.Size && Wait-- > 0) {
        UINT16 DoneHead;
        UINT32 DoneLen;
        while (VirtQueuePopUsed(&gTxQ, &DoneHead, &DoneLen)) {
            VirtQueueFreeDescriptor(&gTxQ, DoneHead);
            gTxDone++;
            (void)DoneLen;
        }
    }

    Head = VirtQueueAllocateDescriptor(&gTxQ);
    if (Head == (UINT16)~0) {
        return -1;
    }
    MemSet(gTxBuf, 0, VIRTIO_NET_HDR_LEN + WireLen);
    MemCpy(gTxBuf + VIRTIO_NET_HDR_LEN, Frame, FrameLen);
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
            HandleIpPacket(Buf + VIRTIO_NET_HDR_LEN, Len - VIRTIO_NET_HDR_LEN);
        }
        VirtQueueFreeDescriptor(&gRxQ, Head);
        if (Ok) {
            ReceiveRefillOne(&gRxQ, Buf);
        } else if (Head < RX_BUF_COUNT) {
            ReceiveRefillOne(&gRxQ, gRxBufData[Head]);
        }
    }
}

static int VirtioFindNet(UINT8 *Bus, UINT8 *Dev, UINT8 *Fn, UINT64 *BarOut) {
    int B;
    int D;
    int F;

    for (B = 0; B < 256; B++) {
        for (D = 0; D < 32; D++) {
            for (F = 0; F < 8; F++) {
                UINT32 VidDid = PciReadConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x00);
                UINT16 Vid = (UINT16)(VidDid & 0xFFFF);
                UINT16 Did = (UINT16)(VidDid >> 16);
                UINT32 BarLo;

                if (Vid != VIRTIO_VENDOR_ID || Did != VIRTIO_DEV_NET) {
                    continue;
                }
                *Bus = (UINT8)B;
                *Dev = (UINT8)D;
                *Fn = (UINT8)F;
                BarLo = PciReadConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x10);
                *BarOut = BarLo & 0xFFFFFFF0ULL;
                return 1;
            }
        }
    }
    return 0;
}

static int VirtioParseCaps(UINT8 Bus, UINT8 Dev, UINT8 Fn,
                           volatile VIRTIO_COMMON_CFG **Common,
                           volatile VIRTIO_NET_CFG **DevCfg,
                           volatile UINT8 **NotifyBase,
                           UINT32 *NotifyMult) {
    UINT8 CapPtr;
    int Found = 0;

    CapPtr = (UINT8)(PciReadConfig(Bus, Dev, Fn, 0x34) & 0xFF);
    if (CapPtr < 0x40) {
        return 0;
    }

    while (CapPtr >= 0x40) {
        UINT32 Hdr = PciReadConfig(Bus, Dev, Fn, CapPtr);
        UINT8 CapId = (UINT8)(Hdr & 0xFF);
        UINT8 CapNext = (UINT8)((Hdr >> 8) & 0xFF);
        UINT8 CapLen = (UINT8)((Hdr >> 16) & 0xFF);
        if (CapId == PCI_CAP_VENDOR && CapLen >= 16) {
            UINT8 CfgType = (UINT8)((Hdr >> 24) & 0xFF);
            UINT32 Body = PciReadConfig(Bus, Dev, Fn, (UINT8)(CapPtr + 4));
            UINT8 BarIdx = (UINT8)(Body & 0xFF);
            UINT32 OffLo = PciReadConfig(Bus, Dev, Fn, (UINT8)(CapPtr + 8));
            UINT32 Off = OffLo & 0xFFFFFFFC;
            UINT32 BarLo = PciReadConfig(Bus, Dev, Fn, (UINT8)(0x10 + BarIdx * 4));
            UINT64 Base = BarLo & 0xFFFFFFF0ULL;
            volatile UINT8 *Mmio;

            if ((BarLo & 0x6) == 0x4 && BarIdx + 1 < 6) {
                UINT32 BarHi = PciReadConfig(Bus, Dev, Fn, (UINT8)(0x10 + (BarIdx + 1) * 4));
                Base |= (UINT64)BarHi << 32;
            }

            if ((BarLo & 1) == 0 && Base != 0) {
                VirtualMemoryMapRange(Base, Base, 0x100000, PTE_PRESENT | PTE_WRITABLE);
            }
            Mmio = (volatile UINT8 *)(UINTN)(Base + Off);

            if (CfgType == VIRTIO_PCI_CAP_COMMON) {
                *Common = (volatile VIRTIO_COMMON_CFG *)Mmio;
                Found |= 1;
            } else if (CfgType == VIRTIO_PCI_CAP_DEVICE) {
                *DevCfg = (volatile VIRTIO_NET_CFG *)Mmio;
                Found |= 2;
            } else if (CfgType == VIRTIO_PCI_CAP_ISR) {
                gIsr = Mmio;
            } else if (CfgType == VIRTIO_PCI_CAP_NOTIFY) {
                UINT32 Mult = PciReadConfig(Bus, Dev, Fn, (UINT8)(CapPtr + 16));
                *NotifyBase = Mmio;
                *NotifyMult = Mult == 0 ? 1 : Mult;
                Found |= 4;
            }
        }
        CapPtr = CapNext;
    }
    return (Found & 7) == 7;
}

static void VirtioSetFeature(UINT64 Feature) {
    UINT32 DevLo;
    UINT32 DevHi;

    gCommon->DeviceFeatureSelect = 0;
    DevLo = gCommon->DeviceFeature;
    gCommon->DeviceFeatureSelect = 1;
    DevHi = gCommon->DeviceFeature;
    Feature &= ((UINT64)DevHi << 32) | DevLo;
    gCommon->DriverFeatureSelect = 0;
    gCommon->DriverFeature = (UINT32)Feature;
    gCommon->DriverFeatureSelect = 1;
    gCommon->DriverFeature = (UINT32)(Feature >> 32);
}

static int VirtioNetStart(UINT8 Bus, UINT8 Dev, UINT8 Fn, UINT64 BarPhys) {
    volatile VIRTIO_COMMON_CFG *Common = 0;
    volatile VIRTIO_NET_CFG *DevCfg = 0;
    volatile UINT8 *NotifyBase = 0;
    UINT32 NotifyMult = 1;
    UINT32 Cmd;
    UINT32 Bar4Lo;
    UINT32 Bar4Hi;
    UINT64 ModernBar;

    Cmd = PciReadConfig(Bus, Dev, Fn, 0x04);
    PciWriteConfig(Bus, Dev, Fn, 0x04, Cmd | 0x07);

    /* Modern virtio-net MMIO is usually BAR4 (64-bit), often above 4GB. */
    Bar4Lo = PciReadConfig(Bus, Dev, Fn, 0x20);
    Bar4Hi = PciReadConfig(Bus, Dev, Fn, 0x24);
    ModernBar = (Bar4Lo & 0xFFFFFFF0ULL) | ((UINT64)Bar4Hi << 32);
    if ((Bar4Lo & 1) == 0 && ModernBar != 0) {
        VirtualMemoryMapRange(ModernBar, ModernBar, 0x10000,
                              PTE_PRESENT | PTE_WRITABLE);
    }
    {
        UINT32 Bar0 = PciReadConfig(Bus, Dev, Fn, 0x10);
        if (Bar0 & 1) {
            gIoPort = (UINT16)(Bar0 & ~3);
        }
    }

    (void)BarPhys;
    if (!VirtioParseCaps(Bus, Dev, Fn, &Common, &DevCfg, &NotifyBase, &NotifyMult)) {
        DebugWrite("net: missing virtio pci caps\n");
        return -1;
    }
    gCommon = Common;
    gDevCfg = DevCfg;

    Common->DeviceStatus = 0;
    __asm__ volatile("mfence" ::: "memory");
    Common->DeviceStatus = VIRTIO_STATUS_ACK;
    Common->DeviceStatus = VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER;
    VirtioSetFeature(VIRTIO_NET_F_MAC | VIRTIO_F_VERSION_1);
    Common->DeviceStatus = VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
                           VIRTIO_STATUS_FEATURES_OK;
    __asm__ volatile("mfence" ::: "memory");
    if ((Common->DeviceStatus & VIRTIO_STATUS_FEATURES_OK) == 0) {
        Common->DeviceStatus = VIRTIO_STATUS_FAILED;
        DebugWrite("net: FEATURES_OK rejected\n");
        return -1;
    }
    if (VirtQueueSetup(&gRxQ, RX_QUEUE_ID, Common, NotifyBase, NotifyMult) != 0) {
        DebugWrite("net: rx queue setup failed\n");
        return -1;
    }
    if (VirtQueueSetup(&gTxQ, TX_QUEUE_ID, Common, NotifyBase, NotifyMult) != 0) {
        DebugWrite("net: tx queue setup failed\n");
        return -1;
    }

    /* Spec: configure + enable virtqueues before DRIVER_OK. */
    VirtQueueEnable(&gRxQ, RX_QUEUE_ID);
    VirtQueueEnable(&gTxQ, TX_QUEUE_ID);
    gRxQ.LastUsedIdx = gRxQ.Used->Idx;
    gTxQ.LastUsedIdx = gTxQ.Used->Idx;
    ReceiveRefillAll();

    Common->DeviceStatus = VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
                           VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK;
    __asm__ volatile("mfence" ::: "memory");

    /* Device ignores notifies until DRIVER_OK — kick RX again so buffers are live. */
    VirtQueueKick(&gRxQ, RX_QUEUE_ID);
    VirtQueueKick(&gTxQ, TX_QUEUE_ID);

    MemCpy(gMac, (const void *)DevCfg->Mac, 6);
    DebugWrite("net: qsz rx=");
    DebugHex32(gRxQ.Size);
    DebugWrite(" tx=");
    DebugHex32(gTxQ.Size);
    DebugWrite(" bar4=");
    DebugHex64(ModernBar);
    DebugWrite("\n");
    return 0;
}

int NetSendIp(UINT32 DstIp, UINT8 Proto, const void *Payload, UINTN PayloadLen) {
    UINT8 DstMac[6];
    UINT8 Frame[ETH_HDR_LEN + IP_HDR_LEN + 1400];
    ETH_HDR *Eth;
    IP_HDR *Ip;
    UINT16 IpTotal;

    if (!gNetOk || Payload == 0 || PayloadLen > 1400) {
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

UINT16 NetChecksum(const void *Data, UINTN Len) {
    return Sum16((const UINT8 *)Data, Len);
}

int NetInit(void) {
    UINT8 Bus;
    UINT8 Dev;
    UINT8 Fn;
    UINT64 Bar;

    gNetOk = 0;
    if (!VirtioFindNet(&Bus, &Dev, &Fn, &Bar)) {
        DebugWrite("net: virtio-net not found\n");
        return 0;
    }
    if (VirtioNetStart(Bus, Dev, Fn, Bar) != 0) {
        DebugWrite("net: virtio init failed\n");
        return 0;
    }
    gNetOk = 1;
    DebugWrite("net: virtio-net up\n");
    return 0;
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

    if (!gNetOk) {
        return;
    }
    if (gIsr) {
        *gIsr = 0;
    }
    while (VirtQueuePopUsed(&gTxQ, &Head, &Len)) {
        VirtQueueFreeDescriptor(&gTxQ, Head);
        gTxDone++;
        (void)Len;
    }
    NetProcessRx();
}

void NetGetMac(UINT8 Mac[6]) {
    MemCpy(Mac, gMac, 6);
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
        DebugWrite("net: not available\n");
        return;
    }
    NetFormatIp(gIp, IpBuf, sizeof(IpBuf));
    DebugWrite("net: mac ");
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
    DebugWrite("net: arp fail");
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
    if (NetParseIp(Host, &Target) != 0) {
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
