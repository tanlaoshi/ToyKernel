# ARM64 HAL

QEMU virt aarch64：自有 Boot（`-kernel` + DTB）、PL011、ramfb / virtio-*。

**PR-A10**：真 MMU（TTBR0 / TCR / SCTLR.M）、`Vectors.S` 缺页路径、`HalPagingSelfTest`。

**PR-A11**：`HalUser*` / `HalFrameSyscall*`（x8/x0..）、Lower-EL `SVC`、内嵌 EL0 自测；用户 VA @`0x100000000`（躲开核 @`0x40000000`）。

**PR-A12**：`HalElfRelocKind` + `HalSyncICache`；本 arch `HELLO.ELF`（`Build/arm64/user/`）；virt `exec HELLO.ELF` → `Hello Ring3`（协作 drain）。

**PR-A13**：virt **GICv2** + **CNTV**（PPI 27）；`HalCpuHalt` = IRQ + `WFI`；横幅 `timer: Arm64 CNTV+GIC irq`。脚本强制 `-M virt,gic-version=2`。

**PR-A14**：PSCI `CPU_ON` + 每核 idle；默认 `-smp 2`（`TOY_VIRT_SMP=1` 单核）；`smp: hello` / `sched: AP entered idle`。

**PR-A15**：`HAL_FRAME` 门面字段 `InstructionPointer`/`StackPointer`；`HalFrameGetInstructionPointer` / `HalFrameGetArgument0..2`（去 x86 `Rip`/`Rsp`/`Arg*` 名）。

构建：`./build.sh arm64` → `Build/arm64/Kernel.elf`；验收 `./run-virt-arm.sh` / `./smoke-virt.sh`。
