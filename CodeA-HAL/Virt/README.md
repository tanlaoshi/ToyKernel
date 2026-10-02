# HAL/Virt — Arm64 / RiscV 共享 virt 实现（PR-R5）

QEMU `virt` 机型公共代码：virtio-mmio / blk / net / input、ramfb、DTB 解析、`HalVideo` 门面。

- **每 arch 保留**：`HalSerial`、页表、Startup/Smp/异常、平台寄存器。
- **MMIO 窗**：Arm64 用 `VirtioMmio.c` 默认值；RiscV 由 Makefile `-DVIRTIO_MMIO_*` 覆盖。
- **帧缓冲绘制**：仍链 `HAL/X64/Drivers/Video.c`（与 PR-V2 相同）。

构建：`./build.sh arm64` / `./build.sh riscv`；验收 `./smoke-virt.sh`。
