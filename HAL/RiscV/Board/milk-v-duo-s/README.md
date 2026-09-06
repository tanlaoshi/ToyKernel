# Board：`milk-v-duo-s`（Milk-V Duo S / SG2000）

> **PR-B3**：第一个真机命令行板包。厂商 U-Boot（**不改源码**）+ SoC UART0 → 串口 `ToyOS ready`。  
> 约定总览：[`HAL/Board/README.md`](../../../Board/README.md)。操作：[`Documents/如何增加板级支持.md`](../../../../Documents/如何增加板级支持.md)。

| 项 | 填写 |
|----|------|
| **板名** | Milk-V Duo S |
| **SoC** | Sophgo SG2000（CV181x 系）；本包跑 **RISC-V C906**（厂商默认固件路径） |
| **Arch** | `RiscV`（`BOARD=milk-v-duo-s`） |
| **固件** | 厂商预装 U-Boot（`cvitek_cv181x` 系）；**不改**厂商树源码 |
| **加载方式** | 见下文；链接地址 `0x80200000`（与 `kernel_addr_r` / `link.ld` 一致） |
| **串口** | UART0 @ `0x04140000`，16550 兼容，`reg-shift=2`，115200；USB-UART 接板载调试口（常见：GND / TX / RX） |
| **能力** | **命令行 only**（`TOY_BOARD_HAS_FRAMEBUFFER=0`）→ 串口壳；跳过 video/gui |
| **课堂关系** | GUI 仍 QEMU `BOARD=virt`；本板 = UART 真机靶 |

---

## 为何是 RiscV 包（不是 Arm64）

SG2000 另有 A53，但 **Milk-V 预装固件默认走 C906 + OpenSBI + U-Boot**。B3 对齐「厂商 U-Boot 不改即可加载」，故落点为 `HAL/RiscV/Board/milk-v-duo-s/`。Arm64 真机包可另开，不混本 PR。

---

## 构建

```bash
cd ToyKernel
./build.sh riscv BOARD=milk-v-duo-s
# 可选：扁平二进制（U-Boot load / go）
make ARCH=riscv BOARD=milk-v-duo-s kernel-bin
# 产物：
#   Build/HAL/RiscV/Kernel.elf
#   Build/HAL/RiscV/Kernel.bin
```

串口-only 全内核（推荐；跳过 video/gui）：

```bash
./build.sh riscv BOARD=milk-v-duo-s
```

> `BRINGUP=1` 仅链 Startup/HalSerial/Hal，当前 RiscV 因 `Hal.o` 仍引用 BootInfo/timer 符号，**不能**单独作为 Duo S 验收路径；用完整构建即可。
---

## 加载约定（厂商 U-Boot）

环境变量（厂商常见值，以板子 `printenv` 为准）：

| 变量 | 常见值 |
|------|--------|
| `kernel_addr_r` | `0x80200000` |
| `fdt_addr_r` | `0x81200000` |
| `fdtfile` | `cv181x_milkv_duos_sd.dtb` |

### 推荐：ELF（`bootelf`）

把 `Kernel.elf` 与厂商 DTB 放到 SD 的 FAT 分区（或 TFTP）：

```text
load mmc 0:1 ${kernel_addr_r} Kernel.elf
load mmc 0:1 ${fdt_addr_r} cv181x_milkv_duos_sd.dtb
fdt addr ${fdt_addr_r}
bootelf -p ${kernel_addr_r}
```

入口约定（与 OpenSBI / virt 相同）：`a0 = hartid`，`a1 = DTB`。Startup 解析 `/memory`；失败则用 `BoardConfig` DRAM fallback。

### 备选：扁平二进制（`go`）

```text
load mmc 0:1 ${kernel_addr_r} Kernel.bin
load mmc 0:1 ${fdt_addr_r} cv181x_milkv_duos_sd.dtb
# 若 go 不传 a1，内核用 BoardConfig RAM 表；有 DTB 时优先 bootelf
go ${kernel_addr_r}
```

### 关于 `booti`

`booti` 需要 **Linux Image 头**。本柱交付 ELF / `.bin`，**不**强制 Image 封装；真机优先 `bootelf` / `go`。

---

## 本包文件

| 文件 | 状态 |
|------|------|
| `README.md` | 本文件 |
| `BoardConfig.h` | UART / 加载地址 / 能力勾选 |
| `Board.h` / `Board.c` | `BoardName()` |
| `NOTES.md` | 真机踩坑与验证边界 |

---

## 验收

- [x] `./build.sh riscv BOARD=milk-v-duo-s` 成功
- [x] `make boards ARCH=riscv` 列出本包
- [x] 默认 `BOARD=virt` 的 `./smoke-virt.sh` **不回归**
- [ ] **真机**：厂商 U-Boot 加载后串口出现 `ToyOS ready`（或 `ToyOS 就绪`）
- [x] **未**做 GUI / video / SD→FAT / 板上网卡
- [x] **Common 无业务改动**（仅能力旗标消费路径；见 PR-B3 归档）

等价横幅（若 Locale 未挂上）：Startup 仍打印 `board: milk-v-duo-s`。

---

## 已知缺口

- 无帧缓冲 → 串口壳；GUI 仍 QEMU virt  
- 无 SD/eMMC Block、无板载 NIC（后置）  
- 无 Linux Image 头 → `booti` 非本柱主路径  
- QEMU **不能**完整模拟 Duo S SoC UART/DRAM；本包在 QEMU 上只验证 **能编过 / 板包可选**  
- 真机未在本 CI 环境实测（无物理板时勾选「真机」项留给板主）

---

## 明确不做

- 重写 BootROM / DRAM / 厂商 U-Boot 源码  
- 与 x86 UEFI 桌面真机（**1.3c**）混仓  
- 在 Duo S 上做桌面 / Gui  
- 把板级 `#ifdef` 或 DTB 解析放进 Common  
