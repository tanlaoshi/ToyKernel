# ARM64 HAL

QEMU virt aarch64：自有 Boot（`-kernel` + DTB）、PL011、ramfb / virtio-*。

**PR-A10**：真 MMU（TTBR0 / TCR / SCTLR.M）、`Vectors.S` 缺页路径、`HalPagingSelfTest`。

**PR-A11**：`HalUser*` / `HalFrameSyscall*`（x8/x0..）、Lower-EL `SVC`、内嵌 EL0 自测；用户 VA @`0x100000000`（躲开核 @`0x40000000`）。

**PR-A12**：`HalElfRelocKind` + `HalSyncICache`；本 arch `HELLO.ELF`（`Build/arm64/user/`）；virt `exec HELLO.ELF` → `Hello Ring3`（协作 drain）。

仍欠（见路线图 1.2f）：GIC/timer（A13）、SMP（A14）。

构建：`./build.sh arm64` → `Build/arm64/Kernel.elf`；验收 `./run-virt-arm.sh` / `./smoke-virt.sh`。
