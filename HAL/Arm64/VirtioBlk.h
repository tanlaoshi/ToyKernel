/*
 * VirtioBlk.h — virtio-blk-device（PR-V4）
 */
#ifndef HAL_VIRTIO_BLK_H
#define HAL_VIRTIO_BLK_H

#include "Block.h"

/* 扫描并注册 Block 后端；成功返回可用盘数（经 BlockInit） */
int VirtioBlkInit(void);

#endif
