/*
 * Board.h — QEMU aarch64 virt 板包门面（PR-B2）
 * B3 起可扩 BoardFillBootInfo / 兼容表；不进 Common。
 */
#ifndef TOY_BOARD_H
#define TOY_BOARD_H

#include "BoardConfig.h"

const char *BoardName(void);

#endif /* TOY_BOARD_H */
