# RISC-V HAL

QEMU virt riscv64：OpenSBI / `-kernel`、UART16550、ramfb / virtio-*。

**PR-A10**：真 MMU（Sv39 `satp`）、`TrapVec.S` 缺页路径、`HalPagingSelfTest`。

**PR-A11**：`HalUser*` / `HalFrameSyscall*`（a7/a0..）、U-mode `ecall`、内嵌自测；用户 VA @`0x100000000`。

**PR-A12**：`HalElfRelocKind` + `HalSyncICache`；本 arch `HELLO.ELF`（`Build/riscv/user/`）；virt `exec HELLO.ELF` → `Hello Ring3`（协作 drain）。

**PR-A13**：OpenSBI **SBI timer**（TIME 扩展，legacy 回退）+ `sie.STIE`；`HalCpuHalt` = SIE + `WFI`；横幅 `timer: RiscV SBI timer irq`。

**PR-A14**：次 hart 在 `KernelEntry` 停车 + soft-release / HSM；每核 idle；默认 `-smp 2`。

**PR-A15**：同上门面帧中立命名（`InstructionPointer`/`StackPointer` + `GetInstructionPointer` / `GetArgument*`）。

构建：`./build.sh riscv` → `Build/riscv/Kernel.elf`；验收 `./run-virt-riscv.sh` / `./smoke-virt.sh`。
