/*
 * BoardConfig.h — Milk-V Duo S（SG2000 / CV181x，PR-B3）
 *
 * 仅 HAL / Board 包含；禁止 Common / Services 直接 include。
 * 厂商预装 U-Boot 为 RISC-V（C906）；本包落在 HAL/RiscV/Board/。
 */
#ifndef TOY_BOARD_CONFIG_H
#define TOY_BOARD_CONFIG_H

#define TOY_BOARD_NAME             "milk-v-duo-s"
#define TOY_BOARD_ARCH             "RiscV"

/* SG2000 UART0：DW APB 16550 兼容，寄存器间距 ×4（reg-shift=2） */
#define TOY_BOARD_UART_BASE        0x04140000ULL
#define TOY_BOARD_UART_REG_SHIFT   2
#define TOY_BOARD_UART_BAUD        115200

/* 与厂商 U-Boot 环境变量对齐（kernel_addr_r / fdt_addr_r） */
#define TOY_BOARD_KERNEL_LOAD      0x80200000ULL
#define TOY_BOARD_DTB_LOAD         0x81200000ULL

/* DTB 缺失时的 DRAM fallback（U-Boot 常见 ~510MiB @0x80000000） */
#define TOY_BOARD_RAM_BASE         0x80000000ULL
#define TOY_BOARD_RAM_SIZE         (510ULL * 1024ULL * 1024ULL)

/* 命令行靶：无 FB / 无 virtio；跳过 video/gui */
#define TOY_BOARD_HAS_FRAMEBUFFER  0
#define TOY_BOARD_HAS_BLOCK        0 /* SD→FAT 后置 */
#define TOY_BOARD_HAS_NET          0
#define TOY_BOARD_CONSOLE_ONLY     1
#define TOY_BOARD_IS_VIRT          0

#endif /* TOY_BOARD_CONFIG_H */
