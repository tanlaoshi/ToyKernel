# x86 UEFI PC / 笔记本目标机（PR-H0）

> 概念与步骤见 [`../../Documents/技术手册.md`](../../Documents/技术手册.md)「Boot / 真机」。本页只留 **机型本地**备注。
>
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
# 推荐脚本（ESP 256MiB + TOYOS 剩余）：
./make-usb-stick.sh --device /dev/sdX --yes --sync   # 首次分区
./sync-usb.sh                                       # 日常同步
# 或手动：将 EFI/ 拷到 ESP，rootfs/ 拷到 TOYOS
```

从固件 Boot Menu 选该 U 盘；成功时屏上应出现桌面（或至少 GOP 清屏 / 壁纸色），串口若有则见 `ToyOS ready`。

---

## 3. H0 验收：亮 GOP

| 步骤 | 期望 |
|------|------|
| 固件加载 `BOOTX64.EFI` | 无「Unsupported」类立刻退出 |
| ToyBoot 找到 GOP + `Kernel.elf` | 失败类 `Print` 仍会打（即使 `TOY_BOOT_DEBUG=0`） |
| 跳入 `KernelMain` | 进核后先深蓝灰清屏，再走模块；`HalVideoSet` 挂帧缓冲 |
| 屏 | **有像素变化**（清屏 / 桌面 / 图标）；即本刀过线 |

真机若停在 `Kernel.elf from TOYOS volume` 且无后续 `loading`/`jump` 行：仍在 Boot。  
若已 `jump` 但仍是 Boot 白字、从不换色：多为进核后缺页（旧 bug：UEFI 高栈 + 仅映射低 512MB）。现已在 `Startup.c` 切到 BSS 早期栈。

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
| 真机无遗留 IDE：需 AHCI/NVMe | **H1 ✅** AHCI；**H5 ✅** NVMe | USB MSC 可后 |
| USB 键盘在部分机箱不响应 | **H2 ✅** 端口普查 + 无 MSI 仍 poll；`ps2-kbd` fallback | 见下 H2 |
| 无 COM1 → 无串口冒烟 | **H3 ✅** GOP 文本镜像 | 见下 H3 |
| 网卡非 virtio | **H4 ✅** e1000（`TOY_NET=e1000`） | Realtek 等可复制范例 |
| Secure Boot / 厂商定制菜单 | 机型相关 | 文档级：关 SB 或签名（后置） |
| 超高分 / 怪异 PixelFormat | ToyBoot 已滤 `PixelBltOnly`、偏小模式 | 记具体机型到本页「机型笔记」 |

### H1：AHCI 第二 Block

- 驱动：`HAL/X64/Drivers/Ahci.c` + `BlockAhci.c`（Driver Block / **D2**）
- 课堂：默认仍 IDE+ATA；验收 `TOY_DISK=ahci ./smoke-boot.sh`（串口 `boot: ahci drives=`）
- 真机：SATA/AHCI 控制器上的 FAT（含 `TOYOS.ID`）可 `ls` / `exec`；**纯 USB 大容量（MSC）本刀不做**
- Common FAT/VFS 无改动

### H2：真机键盘

> **2026-09-09（家里轨 PR1–5）**：`home/xhci-retry-from-scratch` 已并入真机路径：**DiagChk**、**firmware-first 环**、**无 PED=0 禁用**、枚举 + CA 恢复、真机 **`irq=poll (base)`**（零 MSI；`XhciTryEnterDual` 占位）。课堂 QEMU 仍 MSI/dual。

- **PR-H-xhci-obs/rs/port/enum/base ✅**（本分支一次落地；拆分见路线图家里轨）
  - 观测：`xhci OK|FAIL step want=… got=…` + `HalSerialBootMark`
  - 控制器：Halt-only 优先；固件 DCBAAP/CRCR/ERST；`RS running`
  - 端口：USB2 `PR` / USB3 `WPR`；清变更强制带 `PP`；**禁止**复位前写 `PED=0`
  - 枚举：EnableSlot / Address / Config；命令超时 CA 恢复一次
  - 输入：真机 `boot: xhci irq=poll (base)` + Drain；PHOTO 后再 Arm（仍 poll）
- **PR-H-hub ⬜（JX 可选）**：根口 hub 后键盘
- **PR-H-xhci-dual / stat / irq ⬜**：base 通后再开；裸 `irq=msi` 曾致桌面死输入
- **回归笔记**：曾通 poll；USB3 写 PED=0 → `PORTSC=0` / `why=PED clear TO`（已修）
- **PS/2 fallback** / **H-ioapic**：见 H2 其余条目；未做 EHCI/嵌套 hub 等

### H3：无 COM1 → GOP 控制台

- `Serial.c`：scratch 探测 COM1；失败则 `SerialPresent()==0`，不再空转等待 THRE
- `HalSerialWrite`：有 COM1 → 原路径；无 COM1 → 视频前入环，`HalSerialGopEnable`（`InitializeVideo`）后 `HalVideoDrawString` + Present
- 串口有：`boot: COM1 serial ok`（课堂 QEMU / `smoke-boot` 不回归）
- 课堂强制验 GOP 路径：`./build.sh NO_COM1=1` 后 `./run-split.sh`（有显示窗）应见黄字 `ToyOS GOP console (no COM1)`
- **未做**：USB-UART；完整独立 TTY 窗；Arm/RiscV 无此刀

### H5：NVMe Block

- 驱动：`HAL/X64/Drivers/Nvme.c` + `BlockNvme.c`（Driver Block / **D2**）
- PCI class `01.08.02`；Admin + 单 IO 队列；同步轮询；512B LBA；单页 PRP bounce
- 课堂：`TOY_DISK=nvme ./smoke-boot.sh`（串口 `boot: nvme drives=`；双盘=2）
- 真机：PCIe NVMe 上 FAT（含 `TOYOS.ID`）可 `ls` / `exec`；**4KiB LBA / 多 NS / MSI 本刀不做**
- Common FAT/VFS 无改动；注册在 AHCI 之后，有 NVMe 时覆盖后端

### H-msc：USB MSC（分 PR）

- **msc-1/2** ✅ TG：空壳 + Bulk API 壳（BringUp 恒失败）
- **msc-3** ✅ TG 2026-09-12：Shell `msc scan` — 跳过键鼠/hub 口；已 PED 则 Address+读 class（含 class0 接口）后 **DisableSlot**；**不** Force PR / SetConfig / BOT；独立 `gMscScanEp0` 环
- **msc-4** ✅ TG 2026-09-12：Shell `msc claim` — Force PR；hub 子口 / class-0 hub；SetConfig + Bulk IN/OUT；**不** SCSI/FAT；键鼠口跳过
- 顺带：4K Present 分脏矩形 + 条带 cli（Shell 多命令不再饿死 HID）；实心光标 save-under
- **补丁**（同刀续）：第二根口 hub 上 U 盘 — `ProbeSecondHubForMsc`（勿 DisableSlot 外接 hub）；NUC 经 hub claim ok
- 验收：`msc claim` 直插 / 外接 hub 后 `ready=1`；键鼠仍可用
- **msc-5** ✅ TG 2026-09-13：Shell `msc capacity` — BOT INQUIRY + READ CAPACITY(10)；串口 blocks/bsize；**不**分区/FAT；NUC 外接 hub claim+capacity ok
- **下一刀**：msc-6 BlockMux + mount

### H4：真机网卡范例（e1000）

- 驱动：`E1000.c` + `NetE1000.c`；复用 Net.c ARP/ICMP（`NetBindE1000` / `NetInputFrame`）
- PCI 8086:100E 等；TX/RX ring 轮询；无中断
- 课堂：`TOY_NET=e1000 ./smoke-boot.sh` → `boot: e1000`；默认 virtio 不回归
- **无卡不挡桌面**；`lsdev` 见 `e1000`
- **未做**：Realtek、无线、MSI

**机型笔记**（贡献者追加一行即可）：

| 机型 | UEFI | GOP 亮屏 | 键盘 | 盘 | 备注 |
|------|------|----------|------|-----|------|
| NUC7I7DNH | ✅ | ✅ | 🔧（已枚举；打字靠 **H-xhci-base**） | U 盘 FAT | 2026-09-08 后置口。已见 `xhci-hid keyboard/mouse`；见过 `irq=msi` 后桌面死输入。目标：零 MSI 的 `irq=poll (base)` |
| 工业 PC（家用靶） | ✅ | ✅ | 🔧 **poll base**（PR1–5 落地） | U 盘 FAT | 2026-09-09：`firmware-first` + 无 PED=0 + `irq=poll (base)`；验收看 `xhci OK|FAIL` / `RS running` / `xhci-hid keyboard` |
| （例）ThinkPad T480 | ✅ | ✅ / ❌ | USB? | AHCI? | … |

**冒烟勾选表**（上电→Boot→桌面/串口；xHCI/盘/网；交作业用一页总表）：

→ [`../../Documents/真机冒烟清单.md`](../../Documents/真机冒烟清单.md)（**PR-PC-smoke**）

---

## 5. 与代码边界

- **改**：`ToyBoot/`（GOP/EDID）、`HAL/X64/Startup*`（交接）、本笔记。  
- **不改**：Common FAT / Gui / VFS（真机差异留 HAL）。  
- **不与** `HAL/Board/`（Arm/RiscV 命令行板包）混 PR。
