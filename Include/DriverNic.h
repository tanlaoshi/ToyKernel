/*
 * DriverNic.h — 以太网 L2 挂钩（PR-N-nic-l2）
 *
 * 网卡驱动只实现本表，再 NetAttachNic()；勿实现整份 NET_BACKEND。
 * RX：驱动 Poll/Irq 内调用 NetInputFrame()。
 */
#ifndef DRIVER_NIC_H
#define DRIVER_NIC_H

#include "BootTypes.h"

typedef struct {
    /* 发送完整以太网帧（含头）；成功 0 */
    int (*SendFrame)(const UINT8 *Frame, UINTN FrameLen);
    /* 轮询 RX（及 TX 回收）；可空转 */
    void (*Poll)(void);
    void (*GetMac)(UINT8 Mac[6]);
    /*
     * 可选：链路状态。成功 0；不支持则函数指针为 0。
     * Up：1=up；Mbps：速率；FullDuplex：1=全双工。
     */
    int (*GetLink)(int *Up, UINT32 *Mbps, int *FullDuplex);
} NIC_L2;

/* 非 0 = SendFrame/Poll/GetMac 齐全 */
int NicL2OpsValid(const NIC_L2 *Nic);

#endif
