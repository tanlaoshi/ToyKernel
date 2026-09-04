# RISC-V HAL

QEMU virt riscv64：OpenSBI / `-kernel`、UART16550、ramfb / virtio-*。

**PR-A10**：真 MMU（Sv39 `satp`）、`TrapVec.S` 缺页路径、`HalPagingSelfTest`。

仍欠（见路线图 1.2f）：用户态 / syscall 帧（A11）、ELF reloc（A12）、SBI timer（A13）、SMP（A14）。

构建：`./build.sh riscv` → `Build/riscv/Kernel.elf`；验收 `./run-virt-riscv.sh` / `./smoke-virt.sh`。
