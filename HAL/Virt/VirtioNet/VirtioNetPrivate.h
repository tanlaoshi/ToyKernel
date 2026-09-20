/*
 * VirtioNetPrivate.h — MMIO virtio-net 状态（PR-S-virtionet-virt-1）
 */
#ifndef VIRTIO_NET_PRIVATE_H
#define VIRTIO_NET_PRIVATE_H

#include "VirtioNet.h"
#include "VirtioMmio.h"
#include "Driver.h"
#include "DriverNet.h"
#include "Hal.h"

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

extern VIRTIO_MMIO_DEV gRx;
extern VIRTIO_MMIO_DEV gTx;
extern int gNetOk;
extern UINT8 gMac[6];
extern UINT32 gIp;
extern UINT32 gNetHdrLen;
extern ARP_ENTRY gArpCache[ARP_CACHE_SIZE];
extern UINT8 *gRxBuf[RX_BUF_COUNT];
extern UINT8 *gTxBuf;
extern UINT16 gPingSeq;
extern int gPingWait;
extern UINT32 gPingTarget;
extern UINT16 gPingId;
extern UINT32 gTxDone;
extern UINT32 gRxFrames;
extern int gLwIpRx;
extern int gTxBusy;

static inline void MemSet(void *Dst, UINT8 Val, UINTN Len) {
    UINT8 *P = (UINT8 *)Dst;
    UINTN i;
    for (i = 0; i < Len; i++) {
        P[i] = Val;
    }
}

static inline void MemCpy(void *Dst, const void *Src, UINTN Len) {
    UINT8 *D = (UINT8 *)Dst;
    const UINT8 *S = (const UINT8 *)Src;
    UINTN i;
    for (i = 0; i < Len; i++) {
        D[i] = S[i];
    }
}

static inline UINT16 ByteSwap16(UINT16 V) {
    return (UINT16)((V << 8) | (V >> 8));
}

static inline UINT32 ByteSwap32(UINT32 V) {
    return ((V & 0x000000FFu) << 24) | ((V & 0x0000FF00u) << 8) |
           ((V & 0x00FF0000u) >> 8) | ((V & 0xFF000000u) >> 24);
}

static inline UINT16 Sum16(const UINT8 *Data, UINTN Len) {
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

void RxRefillOne(UINT16 DescId, UINT8 *Buf);
void RxRefillAll(void);
void TxDrain(void);
int NetSendFrame(const UINT8 *Frame, UINTN FrameLen);
void ArpLearn(UINT32 Ip, const UINT8 Mac[6]);
void HandleArp(const ARP_PKT *Arp);
void HandleIcmp(const IP_HDR *Ip, const UINT8 *Payload, UINTN PayloadLen);
void NetProcessRx(void);
int NetResolve(UINT32 TargetIp, UINT8 Mac[6], int TimeoutMs);
void VirtioNetPoll(void);
void VirtioNetFormatIp(UINT32 Ip, char *Buf, int BufLen);
int VirtioNetParseIp(const char *Text, UINT32 *Ip);
int VirtioNetSendIp(UINT32 DstIp, UINT8 Proto, const void *Payload, UINTN PayloadLen);
int VirtioNetPing(const char *Host, int TimeoutMs);

#endif
