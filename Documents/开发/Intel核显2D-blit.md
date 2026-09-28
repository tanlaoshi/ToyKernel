# Intel 核显 2D blit（PR-G-igpu · 活文档）

> **目的**：拖窗 / Present 少靠 CPU `memcpy` 往 GOP 搬像素；用 NUC 核显 Blitter 做矩形拷贝。  
> **排期指针**：路线图 ★ [`PR-G-igpu-0`](../路线图.md#pr-g-igpu-0)；柱总览 [`#pr-g-igpu`](../路线图.md#pr-g-igpu)。  
> **权威代码**：`HAL/X64/Drivers/Igpu/` +（后续）`HalVideoCopyRect` / Present 分支。  
> **日期**：2026-09-29 · **igpu-0 ✅ TG**（NUC `0x5917`）· **★ igpu-1 JX**。

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

> **状态**：**★ JX 中**（2026-09-29）。  
> **一句话**：VMM 后映 BAR0 **只读**指纹；错代/拒映 → 软退。

| 项 | 内容 |
| -- | ---- |
| 改 | `IgpuMmio.c`；Video 模块末调用；UC 映；读 Gen6 TIMESTAMP 等 |
| 不改 | 提交；GGTT 写；`IgpuReady` 仍 0 |
| 验收 | NUC `Boot: igpu mmio …`；屏不黑；QEMU skip；`smoke-boot` 绿 |
| 工期 | Agent **1 日** + 手测 **1 轮** → **1～2 日** |
| 下一刀 | igpu-2 |

---

## 7. PR-G-igpu-2 · GGTT 映帧缓冲

> **一句话**：GOP FB（或后缓冲页）挂进 GGTT/aperture，GPU 与 CPU 同看一块像素。  
> **不做**：blit 提交。

| 项 | 内容 |
| -- | ---- |
| 改 | GGTT PTE；WC 与现有 `HalVideoEnableFbWc` 对齐；失败整柱禁用 |
| 不改 | 分辨率；ToyBoot modeset |
| 验收 | `igpu gtt ok`；屏不花不黑；键鼠仍可用 |
| 工期 | Agent **2～3 日** + 手测 **2～4 轮** → **5～10 日**（最肥） |
| 下一刀 | igpu-3 |

---

## 8. PR-G-igpu-3 · Blitter 自测

> **一句话**：Blitter ring + 一次 `XY_SRC_COPY`（或 Gen9 等价）；色块 A→B；超时则复位并禁用。

| 项 | 内容 |
| -- | ---- |
| 改 | ring；提交/等待；Shell/`igpu blit-test` |
| 不改 | Gui 拖窗（igpu-5 再接） |
| 验收 | 目视色块动；连续 10 次无挂；失败回 CPU |
| 工期 | Agent **3～4 日** + 手测 **2～4 轮** → **6～12 日** |
| 下一刀 | igpu-4 |

---

## 9. PR-G-igpu-4 · HalVideo 挂钩

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
