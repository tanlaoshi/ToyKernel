# ARM64 HAL（占位）

本目录为 AArch64 架构 HAL 骨架，尚未实现完整启动与中断。

待实现：UEFI 入口、`Vectors.S`、GIC、Generic Timer、PL011 串口、4 级页表。

构建：`make ARCH=arm64`（当前会因 `Common/VirtualMemory.c` 等 x86 专用代码而失败，需后续分架构编译 Common）。
