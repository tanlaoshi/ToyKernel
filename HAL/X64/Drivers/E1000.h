/*
 * E1000.h — Intel 8254x/e1000 MMIO L2（PR-H4）
 *
 * 仅以太网帧收发；ARP/ICMP 仍在 Net.c。PCI 8086:100E 等。
 */
#ifndef E1000_H
#define E1000_H

#include "BootTypes.h"

int E1000Setup(void);
int E1000Ready(void);
void E1000GetMac(UINT8 Mac[6]);
int E1000SendFrame(const UINT8 *Frame, UINTN Len);
/* 轮询 RX；每帧回调 NetInputFrame（由 Net.c 导出） */
void E1000Poll(void);

#endif
