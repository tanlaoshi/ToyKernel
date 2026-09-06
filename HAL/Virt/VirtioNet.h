/*
 * VirtioNet.h — virtio-net-device MMIO（PR-N9 / PR-D3）
 */
#ifndef HAL_VIRTIO_NET_H
#define HAL_VIRTIO_NET_H

/* PR-D3：向 Drv 注册描述符（不 Probe） */
void VirtioNetRegister(void);
/* Probe Net 类；无卡时 Ready=0 仍返回 0（不拖垮模块） */
int VirtioNetInit(void);

#endif
