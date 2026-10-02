/*
 * VirtioNetRx.c — 接收环与入站分发（PR-S-virtionet-virt-1）
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

void RxRefillOne(UINT16 DescId, UINT8 *Buf) {
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

void RxRefillAll(void) {
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

void NetProcessRx(void) {
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
