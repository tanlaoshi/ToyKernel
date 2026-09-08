/*
 * VirtioBlock.h — virtio-blk-device（PR-V4 / PR-D2）
 */
#ifndef HAL_VIRTIO_BLOCK_H
#define HAL_VIRTIO_BLOCK_H

#include "Block.h"

/* PR-D2：向 Driver 注册描述符（不 Probe） */
void VirtioBlockRegister(void);
/* Probe Block 类 + BlockInit；成功返回可用盘数 */
int VirtioBlockInit(void);

#endif
