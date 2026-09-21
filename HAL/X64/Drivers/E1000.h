/*
 * E1000.h — Intel 8254x / 82574 L2（PR-H4 / H4e-1…3）
 *
 * 仅以太网帧收发；ARP/ICMP 仍在 Net.c。
 * H4e-3：试 MSI RX（失败则 NetPoll 备份）。
 * QEMU：e1000（82540）或 e1000e（82574 Did=10D3）。
 */
#ifndef E1000_H
#define E1000_H

#include "BootTypes.h"

int E1000Setup(void);
int E1000Ready(void);
void E1000GetMac(UINT8 Mac[6]);
/* PR-H4e-2：STATUS 链路；成功 0 */
int E1000GetLink(int *UpOut, UINT32 *MbpsOut, int *FullDuplexOut);
const char *E1000ChipName(void);
int E1000SendFrame(const UINT8 *Frame, UINTN Len);
/* 轮询 RX；每帧回调 NetInputFrame（由 Net.c 导出） */
void E1000Poll(void);
/* PR-H4e-3：MSI 向量入口 */
void E1000Irq(void);
int E1000IrqEnabled(void);
/* PR-N-i219-note：只读 dump；Write 可由 Shell 传 ConsoleWrite */
void E1000DumpNote(void (*Write)(const char *Text));

#endif
