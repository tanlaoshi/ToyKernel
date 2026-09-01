/*
 * Udp.h — 极简 UDP：绑定本地端口、发送、接收队列
 */
#ifndef UDP_H
#define UDP_H

#include "BootConfig.h"

#define UDP_PAYLOAD_MAX 512

typedef struct {
    UINT32 SrcIp;
    UINT16 SrcPort;
    UINT16 DstPort;
    UINT16 Len;
    UINT8  Data[UDP_PAYLOAD_MAX];
} UDP_DATAGRAM;

void UdpInit(void);
int  UdpBind(UINT16 Port);
UINT16 UdpBoundPort(void);
int  UdpSend(UINT32 DstIp, UINT16 DstPort, const void *Data, UINTN Len);
int  UdpRecv(UDP_DATAGRAM *Out);
void UdpInput(UINT32 SrcIp, UINT32 DstIp, const UINT8 *Payload, UINTN Len);

#endif
