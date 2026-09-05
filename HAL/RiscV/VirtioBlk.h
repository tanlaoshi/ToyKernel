/*
 * VirtioBlk.h — virtio-blk-device（PR-V4 / PR-D2）
 */
#ifndef HAL_VIRTIO_BLK_H
#define HAL_VIRTIO_BLK_H

#include "Block.h"

/* PR-D2：向 Drv 注册描述符（不 Probe） */
void VirtioBlkRegister(void);
/* Probe Block 类 + BlockInit；成功返回可用盘数 */
int VirtioBlkInit(void);

#endif
