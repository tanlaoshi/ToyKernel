/*
 * VirtioNet.h — virtio-net-device MMIO（PR-N9）
 */
#ifndef HAL_VIRTIO_NET_H
#define HAL_VIRTIO_NET_H

#include "BootTypes.h"

/* 扫描并初始化；无卡时 Ready=0 仍返回 0（不拖垮模块） */
int VirtioNetInit(void);
int VirtioNetReady(void);
void VirtioNetPoll(void);
void VirtioNetGetMac(UINT8 Mac[6]);
UINT32 VirtioNetGetIp(void);
void VirtioNetFormatIp(UINT32 Ip, char *Buf, int BufLen);
int VirtioNetParseIp(const char *Text, UINT32 *Ip);
int VirtioNetPing(const char *Host, int TimeoutMs);
void VirtioNetGetStats(UINT32 *TxDone, UINT32 *RxFrames);
int VirtioNetSendIp(UINT32 DstIp, UINT8 Proto, const void *Payload,
                    UINTN PayloadLen);
UINT16 VirtioNetChecksum(const void *Data, UINTN Len);
void VirtioNetSetLwIpRx(int Enable);

#endif
