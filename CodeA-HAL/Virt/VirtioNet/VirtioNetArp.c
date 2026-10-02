/*
 * VirtioNetArp.c — ARP/ICMP 与帧发送（PR-S-virtionet-virt-1）
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

void ArpLearn(UINT32 Ip, const UINT8 Mac[6]) {
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

void TxDrain(void) {
    UINT16 Used = *gTx.UsedIdx;
    while (gTx.LastUsed != Used) {
        gTx.LastUsed = (UINT16)(gTx.LastUsed + 1);
        gTxDone++;
        gTxBusy = 0;
    }
}

int NetSendFrame(const UINT8 *Frame, UINTN FrameLen) {
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

void HandleIcmp(const IP_HDR *Ip, const UINT8 *Payload, UINTN PayloadLen) {
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

int NetResolve(UINT32 TargetIp, UINT8 Mac[6], int TimeoutMs) {
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
