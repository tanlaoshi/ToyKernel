#ifndef TOY_TCPCLIENT_H
#define TOY_TCPCLIENT_H

#include "BootTypes.h"

/* 0 ok; -1 connect err; -2 timeout; -3 send err */
int LwIpTcpConnectSend(UINT32 DstIp, UINT16 DstPort,
                       const void *Data, UINTN Len, int TimeoutMs);

#endif
