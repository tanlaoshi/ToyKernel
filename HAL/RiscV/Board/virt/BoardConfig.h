/*
 * BoardConfig.h — QEMU riscv64 virt（PR-B2）
 *
 * 仅 HAL / Board 包含；禁止 Common / Services 直接 include。
 */
#ifndef TOY_BOARD_CONFIG_H
#define TOY_BOARD_CONFIG_H

#define TOY_BOARD_NAME             "virt"
#define TOY_BOARD_ARCH             "RiscV"

/* QEMU virt UART16550 */
#define TOY_BOARD_UART_BASE        0x10000000ULL
#define TOY_BOARD_UART_BAUD        115200

/* QEMU -kernel 链接/加载提示（见 HAL/RiscV/link.ld、Startup.c） */
#define TOY_BOARD_KERNEL_LOAD      0x80000000ULL
#define TOY_BOARD_DTB_LOAD         0x00000000ULL /* 由 OpenSBI/QEMU 交接；非固定 loader */

/* virt 默认可有 ramfb；串口子集由运行时 HalHasFrameBuffer 决定 */
#define TOY_BOARD_HAS_FRAMEBUFFER  1
#define TOY_BOARD_HAS_BLOCK        1 /* virtio-blk */
#define TOY_BOARD_HAS_NET          1 /* virtio-net */
#define TOY_BOARD_CONSOLE_ONLY     0

#endif /* TOY_BOARD_CONFIG_H */
