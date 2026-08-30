# RISC-V HAL（占位）

本目录为 RISC-V 64 架构 HAL 骨架，尚未实现完整启动与中断。

待实现：OpenSBI 入口、`Trap.S`、PLIC、SBI 定时器/控制台、Sv39 页表。

构建：`make ARCH=riscv`（当前会因 `common/Vmm.c` 等 x86 专用代码而失败，需后续分架构编译 common）。
