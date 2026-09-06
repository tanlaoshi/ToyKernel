#ifndef TOY_UDP_H
#define TOY_UDP_H

#include "BootTypes.h"
#include "Udp.h"

int LwIpUdpBind(UINT16 Port);
UINT16 LwIpUdpBoundPort(void);
int LwIpUdpSend(UINT32 DstIp, UINT16 DstPort, const void *Data, UINTN Len);
int LwIpUdpRecv(UDP_DATAGRAM *Out);

#endif
