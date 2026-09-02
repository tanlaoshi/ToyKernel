/*
 * LwIp.h — lwIP 门面（需 make LWIP=1 编译）
 */
#ifndef LWIP_SERVICE_H
#define LWIP_SERVICE_H

#include "BootTypes.h"

int  LwIpInit(void);
void LwIpPoll(void);
int  LwIpActive(void);
int  LwIpPing(UINT32 DstIp, int TimeoutMs);

#endif
