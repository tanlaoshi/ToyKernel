/*
 * TcpPrivate.h — 单连接 TCP 状态（PR-S-tcp-1）
 */
#ifndef TCP_PRIVATE_H
#define TCP_PRIVATE_H

#include "Tcp.h"

#define TCP_HDR_LEN       20
#define TCP_FLAG_FIN      0x01
#define TCP_FLAG_SYN      0x02
#define TCP_FLAG_RST      0x04
#define TCP_FLAG_PSH      0x08
#define TCP_FLAG_ACK      0x10

#define TCP_SND_BUF       2048
#define TCP_RCV_BUF       32768 /* PR-S2：可缓冲一小 ELF + HTTP 头 */
#define TCP_MSS           512
#define TCP_ADV_WND       4096
#define TCP_CLIENT_ADV_MAX 2048 /* 限流：避免 QEMU 用户网突发打满 RX 环丢包 */
#define TCP_RTO_POLLS     8000
#define TCP_MAX_RETRANS   5

typedef struct {
    UINT16 SrcPort;
    UINT16 DstPort;
    UINT32 Seq;
    UINT32 Ack;
    UINT8  DataOff;
    UINT8  Flags;
    UINT16 Window;
    UINT16 Checksum;
    UINT16 Urgent;
} __attribute__((packed)) TCP_HDR;

extern TCP_STATE gState;
extern UINT16 gLocalPort;
extern UINT16 gPeerPort;
extern UINT32 gPeerIp;
extern UINT16 gIss;
extern UINT32 gSndUna;
extern UINT32 gSndNxt;
extern UINT8 gSndBuf[TCP_SND_BUF];
extern UINT32 gSndBufLen;
extern UINT16 gPeerWnd;
extern UINT32 gRcvNxt;
extern UINT32 gPollTicks;
extern UINT32 gRtoDeadline;
extern UINT8 gRetransCount;
extern int gClientMode;
extern UINT8 gRcvBuf[TCP_RCV_BUF];
extern UINT32 gRcvBufLen;
extern UINT32 gRcvTotal;
extern int gPeerClosed;

static inline UINT16 HostToNet16(UINT16 V) {
    return (UINT16)((V >> 8) | (V << 8));
}

static inline UINT32 HostToNet32(UINT32 V) {
    return ((V & 0xFF) << 24) | ((V & 0xFF00) << 8) |
           ((V >> 8) & 0xFF00) | (V >> 24);
}

static inline UINT16 NetToHost16(UINT16 V) {
    return HostToNet16(V);
}

static inline UINT32 NetToHost32(UINT32 V) {
    return HostToNet32(V);
}

static inline void BufCopy(void *Dst, const void *Src, UINTN Len) {
    UINT8 *D = (UINT8 *)Dst;
    const UINT8 *S = (const UINT8 *)Src;
    UINTN i;

    for (i = 0; i < Len; i++) {
        D[i] = S[i];
    }
}

int TcpSendSegment(UINT8 Flags, const void *Data, UINTN Len, UINT32 Seq, UINT32 Ack);
void TcpArmRetrans(void);
void TcpFlushSend(void);
int TcpQueueBytes(const void *Data, UINTN Len);
void TcpProcessAck(UINT32 Ack);
void TcpRetransmit(void);

#endif
