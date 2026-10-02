# 单仓编 Kernel（BOX-7）

> **验收**：只要有本仓 `ToyKernel` + 交叉/本机工具链（`Tools/Extract` 或系统 gcc），即可 `./build.sh`。  
> **不需要**旁挂 EDK2、ToyBoot 源码、ToyImage 才能完成**编译**。

## 步骤

```bash
cd /path/to/ToyKernel          # 例如 $TOYOS_ROOT/ToyKernel 或任意拷贝
# 工具链：已有 Tools/Extract 即可；否则按 Tools/README 拉取
./build.sh                     # 默认 x86_64 → Build/HAL/X64/Kernel.elf（单仓）
# ./build.sh arm64 LWIP=0
# ./build.sh riscv LWIP=0
# 旁有 ToyOS 树根时产物在 $TOYOS_ROOT/Build/ToyKernel/
```

- 若旁边有 `../ToyImage/RootFs/…`：编完会同步 `Kernel.elf`（便利，非硬依赖）。  
- 无 ToyImage：打印 `note: no ../ToyImage/... skip`，**仍算成功**。

## 与 Boot / 镜像的关系（运行时，非编译）

| 产物 | 谁提供 | 说明 |
| ---- | ------ | ---- |
| `Kernel.elf` | 本仓 `./build.sh` | 本刀验收对象 |
| `BOOTX64.EFI` | `$TOYOS_ROOT/ToyBoot` + `$TOYOS_ROOT/EDK2` | BOX-6；加载 Kernel |
| RootFs / QEMU | `$TOYOS_ROOT/ToyImage` | 把 `Kernel.elf` 放进 `RootFs/X64/` 后 `smoke` |

Handoff ABI（`BOOT_INFO` / `BootHandoff.h`）在 **Kernel 仓内自有副本**；与 ToyBoot 侧镜像保持字段一致，编译时**不** `#include` ToyBoot 树。

## 自检（曾用）

```bash
# 任意目录，仅本仓 + Extract 链接
rsync -a --exclude Build --exclude Tools/Extract …/ToyKernel/ /tmp/toykernel-solo/
ln -sfn …/Tools/Extract /tmp/toykernel-solo/Tools/Extract
cd /tmp/toykernel-solo && ./build.sh x86_64
# → Build/HAL/X64/Kernel.elf；无 ../ToyImage 时 skip 同步
```
