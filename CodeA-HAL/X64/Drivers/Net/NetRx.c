/*
 * NetRx.c — 收包与轮询（PR-S-net-1）
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
