# RISC-V HAL

QEMU virt riscv64：OpenSBI / `-kernel`、UART16550、ramfb / virtio-*。

**PR-A10**：真 MMU（Sv39 `satp`）、`TrapVec.S` 缺页路径、`HalPagingSelfTest`。

**PR-A11**：`HalUser*` / `HalFrameSyscall*`（a7/a0..）、U-mode `ecall`、内嵌自测；用户 VA @`0x100000000`。

仍欠（见路线图 1.2f）：ELF reloc（A12）、SBI timer（A13）、SMP（A14）。

构建：`./build.sh riscv` → `Build/riscv/Kernel.elf`；验收 `./run-virt-riscv.sh` / `./smoke-virt.sh`。
