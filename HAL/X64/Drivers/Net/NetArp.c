/*
 * NetArp.c — ARP / ICMP / ping（PR-S-net-1）
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
    UINT32 Ip;
    UINT8  Mac[6];
    int    Valid;
} ARP_ENTRY;

static ARP_ENTRY gArpCache[ARP_CACHE_SIZE];
static UINT16 gPingSeq;
static int gPingWait;
static UINT32 gPingTarget;
static UINT16 gPingId;

static int NetResolve(UINT32 TargetIp, UINT8 Mac[6], int TimeoutMs);

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

void ArpLearn(UINT32 Ip, const UINT8 Mac[6]) {
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

void HandleArp(const ARP_PKT *Arp) {
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

void HandleIcmp(const IP_HDR *Ip, const UINT8 *Payload, UINTN PayloadLen) {
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
