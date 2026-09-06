# x86 UEFI PC / 笔记本目标机（PR-H0）

> 与 **1.3b Board 包**互不混仓。本页只服务 **1.3c**：课堂桌面真机 = UEFI PC，亮屏走 **ToyBoot GOP**。  
> 排期见 [`Documents/路线图.md`](../../Documents/路线图.md) **1.3c**；Boot 说明见 [`ToyBoot/README.md`](../../../ToyBoot/README.md)。

---

## 1. 目标机最小约定

| 项 | 要求 | 备注 |
|----|------|------|
| 固件 | 能进 **UEFI**（CSM/Legacy 仅 BIOS 不够） | Secure Boot 可关；或把 `BOOTX64.EFI` 加入允许列表 |
| 介质 | **U 盘 FAT**（建议 GPT + ESP） | 盘上有 `EFI/BOOT/BOOTX64.EFI`；系统文件可同盘或第二 FAT 卷 |
| 显示 | 固件暴露 **Graphics Output Protocol（GOP）** | ToyBoot 选模式 → 填 `BOOT_CONFIG` → Startup → `BOOT_INFO` |
| 键盘 | **尽量 USB** | H0 不强制能敲键；无键仍以「屏亮了」为过。输入见 **H2** |
| 串口 | **可选** | 多数笔记本无 `0x3F8`；调试见 **H3**（GOP 控制台），勿绑死 COM1 |

**不做本柱靶**：手机 SoC、Duo S（走 **1.3b**）、仅 Legacy BIOS 的老机。

---

## 2. U 盘布局（与课堂双盘同形）

课堂 QEMU：`ToyImage/run-split.sh`（盘0=ESP，盘1=`rootfs/`）。真机可压成 **一盘两分区** 或 **单 FAT**：

```
ESP (FAT):     EFI/BOOT/BOOTX64.EFI
TOYOS (FAT):   TOYOS.ID, Kernel.elf, THEME.CFG, *.ELF, ...
```

ToyBoot **优先**从含 `TOYOS.ID` 的卷加载 `Kernel.elf`（启动盘仅兜底）。制作：

```bash
cd ToyKernel && ./build.sh
cd ../ToyBoot && ./build.sh          # → ToyImage/EFI/BOOT/BOOTX64.EFI
cd ../ToyImage && ./prepare-rootfs.sh
# 将 EFI/ 与 rootfs/ 内容拷到 U 盘对应分区
```

从固件 Boot Menu 选该 U 盘；成功时屏上应出现桌面（或至少 GOP 清屏 / 壁纸色），串口若有则见 `ToyOS ready`。

---

## 3. H0 验收：亮 GOP

| 步骤 | 期望 |
|------|------|
| 固件加载 `BOOTX64.EFI` | 无「Unsupported」类立刻退出 |
| ToyBoot 找到 GOP + `Kernel.elf` | 失败类 `Print` 仍会打（即使 `TOY_BOOT_DEBUG=0`） |
| 跳入 `KernelMain` | `HalVideoSet` 挂上 Boot 传入的帧缓冲 |
| 屏 | **有像素变化**（清屏 / 桌面 / 图标）；即本刀过线 |

QEMU 回归（无真机时）：

```bash
cd ToyKernel && ./build.sh
cd ../ToyImage && ./smoke-boot.sh    # 串口 ToyOS ready
# 有显示时：./run-split.sh          # 窗口内应见桌面
```

---

## 4. 已知缺口（H0 承认、不假装齐）

| 缺口 | 状态 | 后续 |
|------|------|------|
| 真机无遗留 IDE：需 AHCI/NVMe | **H1 ✅** AHCI（`Ahci.c` / `TOY_DISK=ahci`） | NVMe / USB MSC 可后 |
| USB 键盘在部分机箱不响应 | xHCI 型号杂 | **H2** |
| 无 COM1 → 无串口冒烟 | 预期 | **H3** GOP 控制台 |
| 网卡非 virtio | 预期 | **H4** 可选 |
| Secure Boot / 厂商定制菜单 | 机型相关 | 文档级：关 SB 或签名（后置） |
| 超高分 / 怪异 PixelFormat | ToyBoot 已滤 `PixelBltOnly`、偏小模式 | 记具体机型到本页「机型笔记」 |

### H1：AHCI 第二 Block

- 驱动：`HAL/X64/Drivers/Ahci.c` + `BlockAhci.c`（Driver Block / **D2**）
- 课堂：默认仍 IDE+ATA；验收 `TOY_DISK=ahci ./smoke-boot.sh`（串口 `boot: ahci drives=`）
- 真机：SATA/AHCI 控制器上的 FAT（含 `TOYOS.ID`）可 `ls` / `exec`；**纯 USB 大容量（MSC）本刀不做**
- Common FAT/VFS 无改动

**机型笔记**（贡献者追加一行即可）：

| 机型 | UEFI | GOP 亮屏 | 键盘 | 盘 | 备注 |
|------|------|----------|------|-----|------|
| （例）ThinkPad T480 | ✅ | ✅ / ❌ | USB? | AHCI? | … |

---

## 5. 与代码边界

- **改**：`ToyBoot/`（GOP/EDID）、`HAL/X64/Startup*`（交接）、本笔记。  
- **不改**：Common FAT / Gui / VFS（真机差异留 HAL）。  
- **不与** `HAL/Board/`（Arm/RiscV 命令行板包）混 PR。
