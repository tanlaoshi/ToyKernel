/*
 * BoardConfig.h — QEMU aarch64 virt（PR-B2）
 *
 * 仅 HAL / Board 包含；禁止 Common / Services 直接 include。
 * HalSerial 用 TOY_BOARD_UART_BASE；能力旗标仍由 HalHasFrameBuffer 等门面暴露。
 */
#ifndef TOY_BOARD_CONFIG_H
#define TOY_BOARD_CONFIG_H

#define TOY_BOARD_NAME             "virt"
#define TOY_BOARD_ARCH             "Arm64"

/* QEMU virt PL011 */
#define TOY_BOARD_UART_BASE        0x09000000ULL
#define TOY_BOARD_UART_BAUD        115200

/* QEMU -kernel 链接/加载提示（见 HAL/Arm64/link.ld、Startup.c） */
#define TOY_BOARD_KERNEL_LOAD      0x40000000ULL
#define TOY_BOARD_DTB_LOAD         0x4a000000ULL

/* virt 默认可有 ramfb；串口子集由运行时 HalHasFrameBuffer 决定 */
#define TOY_BOARD_HAS_FRAMEBUFFER  1
#define TOY_BOARD_HAS_BLOCK        1 /* virtio-blk */
#define TOY_BOARD_HAS_NET          1 /* virtio-net */
#define TOY_BOARD_CONSOLE_ONLY     0

#endif /* TOY_BOARD_CONFIG_H */
