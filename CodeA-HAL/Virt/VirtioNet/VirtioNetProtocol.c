/*
 * VirtioNetProtocol.c — SendIp / Ping（PR-S-virtionet-virt-1）
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
