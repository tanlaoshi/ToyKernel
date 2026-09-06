# Board 包约定（PR-B0）

> **一句话**：别人加板只交 `HAL/<Arch>/Board/<board>/`；**Common / Gui / FAT diff 为空**；**不改**厂商 U-Boot / BootROM。  
> 概念（UEFI vs U-Boot、DTB、Startup）：[`Documents/启动与板级支持.md`](../../Documents/启动与板级支持.md)。  
> **逐步操作**：[`Documents/如何增加板级支持.md`](../../Documents/如何增加板级支持.md)。  
> 排期：[`Documents/路线图.md`](../../Documents/路线图.md) **1.3b**（B0～B3）。**x86 桌面真机见 1.3c，勿与本树混仓。**

---

## 1. 目录落点

```text
HAL/
├── Board/
│   ├── README.md              ← 本文（约定 + 清单）
│   └── _template/             ← 复制起点（不要在 _template 下交真板）
├── <Arch>/                    # Arm64 | RiscV（命令行板）；页表/异常/IRQ 形状在此
│   └── Board/
│       └── <board>/           # 例：virt、milk-v-duo-s
│           ├── README.md      # 必填：加载约定、UART、验收
│           ├── BoardConfig.h  # 建议：地址 / 能力勾选
│           ├── Board.c        # 可选：填 BOOT_INFO 辅助、兼容表
│           └── NOTES.md       # 可选：真机踩坑
├── Virt/                      # QEMU virt 共享（非 Board 包；见 HAL/Virt/README.md）
└── X64/                       # x86 UEFI 课堂 / 真机桌面 → 1.3c，不进 B 系列
```

- **Arch vs Board**：MMU / 异常 / IRQ 控制器形状留在 `HAL/<Arch>/`；内存图、串口基址、模块子集、DTB 兼容串在 Board。  
- **设备 vs 板**：声卡等走 Driver（[`驱动框架.md`](../../Documents/驱动框架.md)）；板只声明「有这颗设备」并注册，不改 Services。  
- **Makefile `BOARD=`**：**B2 ✅** — `BOARD=virt`（默认）→ `HAL/<Arch>/Board/<board>/`；`make boards`；缺包报错。

---

## 2. 最小文件清单

| 文件 | 必填？ | 职责 |
|------|--------|------|
| `README.md` | **是** | 板名、Arch、固件、加载命令、UART、已知缺口、验收口径 |
| `BoardConfig.h` | 建议 | `UART` 基址、加载/内核链接提示、能力勾选（无 FB → 串口壳） |
| `Board.c` / `Board.h` | B2+ | Startup 调用的填充辅助；兼容 ID 表；**不**进 Common |
| `NOTES.md` | 可选 | 真机踩坑（引脚、分区、厂商环境变量） |
| `dts/` 或链接到厂商 `.dtb` | 可选 | 仅文档/脚本引用；**Common 禁止解析 DTB** |

复制模板：

```bash
cp -a HAL/Board/_template HAL/Arm64/Board/milk-v-duo-s   # 例；再 ./build.sh arm64 BOARD=milk-v-duo-s
# 编辑该目录 README.md / BoardConfig.h，删掉 .example 后缀
```

---

## 3. 贡献者只交什么

**交**

- 本板目录下的 README / Config / Startup 填充 / UART / 兼容表  
- 必要时本 Arch HAL 的入口约定小改（保存 `x0=DTB` 等）——尽量薄  
- U-Boot **环境变量 / 脚本**说明（不改厂商树源码）

**不交 / 不改**

- `Common/`、`User/`、Gui / FAT / VFS 业务  
- 厂商 U-Boot / BootROM / DRAM 初始化源码  
- 把 U-Boot 头或 DTB 解析引进 Common  
- 与 **1.3c**（x86 UEFI 桌面 PC）混在同一 PR  
- 在 Duo S 等命令行靶上做 GUI（GUI 仍 QEMU virt；桌面真机见 H 系列）

验收 diff 经验法则：`git diff --stat` 里 **Common 应为空**（能力旗标 **B1** 除外，且 B1 是跨板通用 HAL 门面，不是某一板包）。

---

## 4. U-Boot 验收口径（命令行板）

最小里程碑（对齐 **B3** / Duo S）：

1. 厂商 U-Boot（不改源码）能 `load` + `booti`（或等价）加载内核 + DTB。  
2. 串口出现 `ToyOS ready`（或本板 README 写明的等价横幅）。  
3. `BOOT_INFO.Regions` 与 DRAM 大致一致（来自 DTB `/memory` 或板级表）。  
4. **本柱跳过** video / gui / SD→FAT / 板上网卡（后置）。

概念命令（板 README 必须改成该机真实变量）：

```text
load mmc 0:1 ${kernel_addr_r} Image
load mmc 0:1 ${fdt_addr_r} board.dtb
booti ${kernel_addr_r} - ${fdt_addr_r}
```

合流点永远是 `BOOT_INFO` → `KernelMain`（见 [`架构分层.md`](../../Documents/架构分层.md) §3）。

---

## 5. 与后续 PR

| PR | 本约定如何用 |
|----|----------------|
| **B0**（约定） | 清单 + `_template`；概念文互指 |
| **B1 ✅** | Common 消费 `HalHasFrameBuffer` / `HalConsoleOnly`，板包勾选能力 |
| **B2 ✅** | `BOARD=` 选中 `HAL/<Arch>/Board/<board>`；默认已收 `virt` |
| **B3** | Duo S：厂商 U-Boot + SoC UART hello |

手机 SoC **明确不做**（不进 B/H）。
