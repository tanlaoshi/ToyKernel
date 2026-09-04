# ARM64 HAL

QEMU virt aarch64：自有 Boot（`-kernel` + DTB）、PL011、ramfb / virtio-*。

**PR-A10**：真 MMU（TTBR0 / TCR / SCTLR.M）、`Vectors.S` 缺页路径、`HalPagingSelfTest`。

仍欠（见路线图 1.2f）：用户态 / syscall 帧（A11）、ELF reloc（A12）、GIC/timer（A13）、SMP（A14）。

构建：`./build.sh arm64` → `Build/arm64/Kernel.elf`；验收 `./run-virt-arm.sh` / `./smoke-virt.sh`。
