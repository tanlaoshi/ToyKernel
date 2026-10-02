/*
 * LwIpPrivate.h — LwIp*.c 内部（PR-N-nic-addr）
 */
#ifndef LWIP_PRIVATE_H
#define LWIP_PRIVATE_H

#ifdef TOY_LWIP
void LwIpConfigPushDns(void);
int  LwIpConfigBindNetif(void);
void LwIpConfigLogDns(void);
#endif

#endif
