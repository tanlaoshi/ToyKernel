# Intel 核显 2D blit（PR-G-igpu · 活文档）

> **目的**：拖窗 / Present 少靠 CPU `memcpy` 往 GOP 搬像素；用 NUC 核显 Blitter 做矩形拷贝。  
> **排期指针**：路线图 ★ [`PR-G-igpu-4`](../路线图.md#pr-g-igpu-4)；柱总览 [`#pr-g-igpu`](../路线图.md#pr-g-igpu)。  
> **权威代码**：`HAL/X64/Drivers/Igpu/` +（后续）`HalVideoCopyRect` / Present 分支。  
> **日期**：2026-09-29 · **igpu-0/1/2/3 ✅ TG** · **★ igpu-4**。

---

## 0. 一句话结论

| 问 | 答 |
| -- | -- |
| 能不能「用显卡」拖窗？ | 真机 GOP 进核后 **没有** 可用的固件 Blt；要自己写核显 2D。 |
| 独显？ | **不做**。 |
| 靶机？ | **NUC7i7DN H**（壳标为准）+ **i7-8650U** + **UHD 620** = **Gen9.5**（按 Gen9 一族估）。 |
| 总工期？ | **约 18～28 日历日**（顺 3～4 周；GGTT/ring 卡死可到 5～6 周）。 |
| 谁干活？ | **Agent** 写码 + QEMU；**你** NUC 手测。 |

---

## 1. 现状与红线

**现在**：后缓冲 CPU 画 → `HalVideoPresent` / `CopyRect` → `memcpy` 到 GOP scanout。快拖大窗时 CPU→FB 带宽不够，左右易闪。

**红线**

- 只许进 **HAL**（手册：GPU 禁止进 Common Gui）。
- **不**自己 modeset / 改分辨率（继续 ToyBoot GOP 选模）。
- **不**多代通用 i915；**不**独显；硬件光标另柱。
- 任一步失败 → **软退**，桌面仍走 CPU 路径；不得挡 `ToyOS ready`。

**为何不用 GOP `Blt`**：`ExitBootServices` 后协议未定义（与 live `SetMode` 同坑）。

---

## 2. 靶机钉死

| 项 | 值 |
| -- | -- |
| 机型 | Intel **NUC7i7DN H**（后壳丝印） |
| CPU | **i7-8650U**（Kaby Lake Refresh） |
| 核显 | **UHD Graphics 620** |
| 架构 | **Gen9.5**（本柱文档/寄存器按 **Gen9** 族） |
| PCI | Vendor `8086` + 类 `0300`；DID 以实机 `igpu-0` 打印为准（白名单钉死） |

QEMU 无此卡 → 整柱软退；Virt/Arm/RiscV **不编**或空桩。

---

## 3. 分工与工期单位

| 角色 | 做啥 |
| ---- | ---- |
| Agent | 实现、`./build.sh`、`smoke-boot`；改文档/串口黄字 |
| 你 | 刷 U 盘、NUC 手测；黑屏时看串口 / 断电复位 |

| 单位 | 含义 |
| ---- | ---- |
| **日历日** | Agent 当日能合完一刀并过 QEMU |
| **手测轮** | 你当晚 NUC 验一次；失败则次日 Agent 修，再 +1 轮 |

纯 Agent 码时约 **12～16 人日**；其余在真机迭代。

---

## 4. 工期总表

| 刀 | 一句话 | Agent 码 | NUC 手测 | 日历（顺） | 风险 |
| -- | ------ | -------- | -------- | ---------- | ---- |
| [igpu-0](#5-pr-g-igpu-0--pci-认卡) | PCI 认卡 + `lsdev` | 0.5～1 日 | 1 轮 | **1～2 日** | 低 |
| [igpu-1](#6-pr-g-igpu-1--mmio-指纹) | BAR 只读指纹 + 软退 | 1 日 | 1 轮 | **1～2 日** | 低 |
| [igpu-2](#7-pr-g-igpu-2--ggtt-映帧缓冲) | GGTT 映 GOP FB | 2～3 日 | 2～4 轮 | **5～10 日** | **高** |
| [igpu-3](#8-pr-g-igpu-3--blitter-自测) | Blitter ring + 色块拷 | 3～4 日 | 2～4 轮 | **6～12 日** | **高** |
| [igpu-4](#9-pr-g-igpu-4--halvideo-挂钩) | CopyRect/Present 挂钩 | 1～2 日 | 1～2 轮 | **2～4 日** | 中 |
| [igpu-5](#10-pr-g-igpu-5--拖窗验收) | 拖窗手感验收 | 1 日 | 2 轮 | **2～3 日** | 中 |
| **合计** | 快拖可感加速 | **~12～16 人日** | **~9～14 轮** | **~18～28 日** | — |

**关键路径**：0→1 可连做；**2 与 3 不要跟别的真机刀并行**（抢 NUC）。

```text
周1      igpu-0 + igpu-1     认卡稳
周2～3   igpu-2              GGTT（可能反复刷盘）
周3～4   igpu-3              ring（可能反复）
周5      igpu-4 + igpu-5     挂钩 + 拖窗手感
```

体量接近「iwl 认卡→关联」那种多周真机柱，不是隔夜小刀。

---

## 5. PR-G-igpu-0 · PCI 认卡

> **状态**：**✅ TG**（2026-09-29；NUC `did=0x5917` @ `00:02.0`）。  
> **一句话**：Intel VGA/Display PCI Probe，打 DID，`lsdev` 可见；**不碰** 命令提交。

| 项 | 内容 |
| -- | ---- |
| 改 | `HAL/X64/Drivers/Igpu/Igpu.c`；`TOY_DRIVER_CLASS_DISPLAY`；`HalDevices` 注册并 Probe |
| 不改 | `HalVideo*` 热路径；Gui |
| 验收 | NUC `Boot: igpu probe did=0x5917` + bound；QEMU soft-fail；`smoke-boot` 绿 |
| 工期 | Agent **0.5～1 日** + 手测 **1 轮** → **1～2 日** |
| 下一刀 | igpu-1 |

---

## 6. PR-G-igpu-1 · MMIO 指纹

> **状态**：**✅ TG**（2026-09-29；NUC `bar=0xDE000000 sz=2MiB ts=0`）。  
> **一句话**：VMM 后映 BAR0 **只读**指纹；`ts=0`（无 forcewake）可接受。

| 项 | 内容 |
| -- | ---- |
| 改 | `IgpuMmio.c`；Video 模块末调用；UC 映；读 TIMESTAMP |
| 不改 | 提交；GGTT 写；可 blit Ready |
| 验收 | NUC `Boot: igpu mmio …`；屏不黑；QEMU skip；`smoke-boot` 绿 |
| 工期 | Agent **1 日** + 手测 **1 轮** → **1～2 日** |
| 下一刀 | igpu-2 |

---

## 7. PR-G-igpu-2 · GGTT / 固件 scanout 观察

> **状态**：**✅ TG**（2026-09-29；NUC `surf=0` + `fb=0xC0000000` 软退）。  
> **NUC**：`gtt surf=0 (observe-only, soft)` + `gtt fb=0xC0000000`（与 `FB-PTE Phys` 一致）；屏未黑。  
> **解读**：同 igpu-1 的 `ts=0`——**无 forcewake 时 PLANE_SURF 常读 0**；本刀**禁止写 PTE**，软退即验收。确认 GPU 可见地址推迟到 **igpu-3**（forcewake + blitter）。  
> **一句话**：**不写 GGTT PTE**；只读 plane SURF / 记 FB phys。  
> **不做**：改 PTE；blit 提交；forcewake。

| 项 | 内容 |
| -- | ---- |
| 改 | `IgpuGtt.c`；只读 `0x7019C/7119C/7219C`；对照 `HalVideoFrameBufferBase` |
| 不改 | 分辨率；Gui；PTE |
| 验收 | ✅ NUC `Boot: igpu gtt …`；屏不黑；`surf=0` 软退 OK；`smoke-boot` 绿 |
| 工期 | Agent **2～3 日** + 手测 **2～4 轮** → **5～10 日** |
| 下一刀 | igpu-3 |

---

## 8. PR-G-igpu-3 · forcewake + Blitter 自测

> **状态**：**✅ TG**（2026-09-29；NUC `blit mem ok`）。  
> **通关**：B13 `fence=0xB13B13B1` `blt=0x00FF00FF` → **`igpu blit mem ok`**。  
> **根因**：C4——旧 `WaitHead(~0x3F)` + TAIL 未 64B 对齐 → B4～B11 假完成；包本身（BR13 depth32）无误。  
> **一句话**：forcewake → GSM → BCS；ring 上 `XY_COLOR_BLT` 已写通 scratch。

| 项 | 内容 |
| -- | ---- |
| 改 | BAR 16MiB；`IgpuForcewake` / `IgpuGsm` / `IgpuBlit`（64B 对齐 WaitHead） |
| 不改 | Gui 拖窗；改 gtt0 PTE；batch（暂搁） |
| 验收（本刀） | ✅ NUC：`blit mem ok`；屏不黑；`smoke-boot` 绿 |
| 下一刀 | igpu-4 |

---

## 8.1 短刀试探台账（igpu-3 · 2026-09-29）

> 凡 NUC 手测过的试探都记；避免重复踩坑。状态：✅过 / ❌否 / 🟡部分 / ⏳未上盘。

### A. 基础设施（已过）

| # | 试探 | 结果 | 串口/现象 | 结论 |
| - | ---- | ---- | --------- | ---- |
| A1 | forcewake GT ACK=`0x0D88`（误 Media） | ❌ | `fw gt ack to` | ACK 应用 **`0x130044`**（i915 `FORCEWAKE_ACK_GT_GEN9`） |
| A2 | forcewake GT=`0x130044` + Render；已醒则跳过 clr | ✅ | `fw ok ts=非0` | 无 wake 时 `ts=0` 正常；wake 后 TIMESTAMP 有效 |
| A3 | 无 wake 读 PLANE_SURF | 🟡 | `surf=0`，`ctl=0x84000000` | plane **已开**；SURF=0 = **GGTT 偏移 0**，非失败 |
| A4 | 接受 gtt0 + 记 FB phys `C0000000` | ✅ | `gtt ok … (gtt0)` | scanout 走 GTT[0] |
| A5 | BAR 映 2MiB → 扩 **16MiB**（GSM 在 +8MiB） | ✅ | `mmio sz=0x01000000` | Gen8+ GTTMMADR=16MiB；GSM=`BAR+size/2` |
| A6 | 读 GSM PTE0 | ✅ | `pte0=0x8C000001` | 固件把 GTT0 → phys **`0x8C000000`**（非 CPU LFB `C0000000`；后者多为 aperture） |
| A7 | BCS ring @`0x22000`，ring 缓冲 GTT`0x01000000`，MI_NOOP | ✅ | `blit ring ok` | HEAD 能追上 TAIL；ring 通路通 |
| A8 | ring 启用时写 HEAD 复位再发色块 | ❌ | `color to` | **禁止**运行中改 HEAD；须停 CTL 或追加 TAIL |
| A9 | Gen8+ `XY_COLOR_BLT` 7 dword（BR13 depth32） | ✅ | 见 B13 | 包正确；旧「未写」是 WaitHead 假完成 |

### B. 像素写验证

| # | 试探 | 结果 | 串口/现象 | 结论 |
| - | ---- | ---- | --------- | ---- |
| B1 | Video 模块内对 **GTT0/scanout** 填品红 @(16,16) | 🟡 | 曾 `color ok pix=FA398DA4` | 后缓冲/桌面覆盖；**不能当验收** |
| B2 | 桌面 Ready 后再画 **右上角** 160×80 | ❌/未见 | 常无 `color at` | 同上 |
| B3～B7 | scratch COLOR / depth / SRC_COPY（**旧 WaitHead**） | ⚠作废 | 曾见 `blt=A5` | 假完成；**以 B13 为准** |
| B8 | `MI_FLUSH_DW\|USE_GTT\|STOREDW` | ❌ | `ipehr=0x13004006` | **禁用该 FLUSH 编码** |
| B9～B11 | BB_START / len 试探 | ⚠作废 | 见 C4 | 被假 WaitHead 污染 |
| B12 | 64B 对齐真 WaitHead；BB_START | 🟡 | store✅；`bbstart to`；ipehr 垃圾 | **环同步✅**；batch 暂搁 |
| B13 | ring **COLOR + STORE fence**（真等 HEAD） | ✅ | `fence=0xB13B13B1` `blt=0x00FF00FF` → **`blit mem ok`** | **2D 写通**；igpu-3 mem 验收过 |

### C. 工程坑（非硬件）

| # | 试探/事故 | 结果 | 结论 |
| - | --------- | ---- | ---- |
| C1 | `KernelModules.c` 已加 `GttInit` 但 `.o` 未重编就 sync | ❌ | 真机无 `gtt` 行；**改调用链要 `rm` 对应 `.o` 或确认 elf 反汇编** |
| C2 | U 盘未挂仍以为 TBU 成功 | ❌ | sync 脚本报错；手测前对 md5 |
| C3 | 屏上色块当验收 | ❌ | 桌面必盖；改 **mem 回读** |
| C4 | `WaitHead` 掩 `~0x3F` + TAIL 未对齐 | ❌→✅已修 | **提交垫 64B 再等 HEAD**；B12/B13 验证 |

### D. 下一步

| # | 试探 | 目的 |
| - | ---- | ---- |
| D1 | ~~TG igpu-3~~ | 已入库 |
| D2 | igpu-4：`HalVideoCopyRect` / Present 挂钩 | 拖窗走 blitter |
| D3 | batch / SRC_COPY 可选补 | 非门禁 |

### E. 关键寄存器速查（Gen9 / 本柱）

| 名 | 偏移 | 备注 |
| -- | ---- | ---- |
| TIMESTAMP | `0x2358` | 需 forcewake 才非 0 |
| FORCEWAKE_GT / ACK | `0xa188` / **`0x130044`** | 勿用 `0x0D88`（Media） |
| FORCEWAKE_RENDER / ACK | `0xa278` / `0x0D84` | |
| PLANE_CTL/SURF_A | `0x70180` / `0x7019C` | ctl bit31=enable |
| GSM | BAR+8MiB | 16MiB BAR 后半 |
| GFX_FLSH_CNTL_GEN6 | `0x101008` | PTE 写后 flush |
| BCS RING_* | base `0x22000` +`0x30/34/38/3c` | |

---

## 9. PR-G-igpu-4 · HalVideo 挂钩

> **状态**：**★**（接 igpu-3）。  
> **一句话**：`IgpuReady` 时大矩形走 blitter，否则 `memcpy`；QEMU 永远 CPU。

| 项 | 内容 |
| -- | ---- |
| 改 | 仅 `HAL/X64` Video 后端；Common 尽量零改 |
| 不改 | 脏矩形语义；Arm/RiscV/Virt |
| 验收 | `smoke-boot`；开窗关窗无花；可 `IGPU=0` 对比 |
| 工期 | Agent **1～2 日** + 手测 **1～2 轮** → **2～4 日** |
| 下一刀 | igpu-5 |

---

## 10. PR-G-igpu-5 · 拖窗验收

> **一句话**：快拖大窗可跟手；相对 CPU 路径闪与滞后可感改善。

| 项 | 内容 |
| -- | ---- |
| 改 | 必要时调 Slide/Present 吃 blit；修残影 |
| 不改 | 标题栏语义；THEME |
| 验收 | 大 Settings 快拖跟手；无黑屏；禁 igpu 仍可拖（慢但稳） |
| 工期 | Agent **1 日** + 手测 **2 轮** → **2～3 日** |
| 下一刀 | 柱收官（仍不默认占 ★） |

---

## 11. 验收总清单（柱完）

- [ ] NUC 串口有 igpu 认卡 / 就绪或明确软退原因  
- [ ] `smoke-boot` 绿；无卡/禁 igpu 行为与今日一致  
- [ ] `igpu blit-test`（或等价）色块通过  
- [ ] 拖窗手感优于纯 CPU（主观 + 可选 `Gui: drag frames`）  
- [ ] 黑屏/挂死路径：禁用后冷启可恢复桌面  

---

## 12. 相关文档

| 文档 | 用途 |
| ---- | ---- |
| [`../路线图.md`](../路线图.md) 文首候补 / `#pr-g-igpu` | 排期指针（细表以**本文**为准） |
| [`../技术手册.md`](../技术手册.md) | GPU 只许 HAL |
| [`../../HAL/X64/NOTES-UEFI-PC.md`](../../HAL/X64/NOTES-UEFI-PC.md) | NUC 真机笔记 |
| [`开机流程与加速.md`](开机流程与加速.md) | 亮屏仍走 GOP，本柱不改 Boot 选模 |
