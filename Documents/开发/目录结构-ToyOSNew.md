# OpenBox 大目录结构（原 ToyOSNew 思路 · 已拍板）

> **状态**：M1 已确认（2026-10-07）。工作区 **`~/OpenBox`**（**不用** `ToyOSNew`）。  
> **现网** `~/ToyOS` 只读对照，确认前不改旧仓源码/构建。  
> **git**：GitHub 单仓 **`OpenBox`** — `git@github.com:tanlaoshi/OpenBox.git`（`~/OpenBox` 已跟踪 `origin/main`）。  
> **文档**：**一个都不从旧仓搬**；结构钉死后迁代码，文档在新树**从新写**。  
> **与旧草案**：[`目录树与可读性方案.md`](目录树与可读性方案.md) 顶层设想仍被本文取代；四层驱动等细部迁代码后再接。

---

## 0. 硬规矩（已确认）

| # | 规矩 |
| - | ---- |
| 1 | 逻辑根名仍是 ToyOS 故事；物理工作区 / 仓名 **`OpenBox`**。下三夹：**Boot**、**Kernel**、**Runtime** |
| 2 | **无**顶层 `Build/`；Boot 产物在各架构 **`Boot/<Arch>/Build/`**（格式不同，不混放）；Kernel 在 **`Kernel/Build/`** |
| 3 | 根**只有** `README.md`（无根 `Documents/`、无根 `Scripts/`、无根 `Config.txt`） |
| 4 | **Boot 按三架构分夹**：`X64` / `Arm64` / `RiscV` |
| 5 | **EDK2** 放在 **`Boot/X64/EDK2/`**，与原 ToyBoot 源码**平级**，作 **X64 工具包**；**剥掉其独立 `.git`**，只作 OpenBox 单仓里的普通目录 |
| 6 | **删 Config.txt**；默认 arch/target 写进各侧 **`*.sh`** |
| 7 | **Scripts 全拆**进 Boot / Kernel（及日后 Runtime 跑盘脚本），根不留 Scripts 院 |
| 8 | **文档零迁移**；先确认树 → **搬代码** → 文档新写 |
| 9 | 每次迁任何东西前：拟清单 → **你确认** → 再动手（本轮文档侧：清单恒为空，只迁代码时仍要确认） |
| 10 | **代码逐步迁移**；每刀可审（你做 code review）；不搞整树一次性搬家 |

---

## 1. 目标顶层

```text
OpenBox/                 # ~/OpenBox ；GitHub: OpenBox
  README.md              # 唯一总入口（地图 + 怎么编/跑；从新写）
  Boot/
    X64/                 # 原 ToyBoot 源码平铺于此
      EDK2/              # 工具包（裁剪 EDK2，与 Boot 源码平级）
      …                  # Boot.c / build.sh 等
    Arm64/               # 含本侧 Build/（.o）
    RiscV/               # 含本侧 Build/（.o）
  Kernel/                # 原 ToyKernel；Build/ 自产自消
    Build/
  Runtime/               # 原 ToyImage：RootFs / ESP / 种子…（无 Build）
```

| 目录 | 人话 |
| ---- | ---- |
| Boot | 机器怎么把 Kernel 拉起来（分架构） |
| Kernel | OS 怎么写、怎么编 |
| Runtime | 编出来的怎么摆盘、怎么跑 |

浏览序：`Boot` → `Kernel` → `Runtime`（故事线）。

---

## 2. 与旧仓对照

| 旧（`~/ToyOS`） | 新（`~/OpenBox`） | 备注 |
| --------------- | ----------------- | ---- |
| `ToyBoot/` | `Boot/X64/`（主体） | Arm/RiscV 引导进对应夹 |
| （EDK2 / 裁剪树） | `Boot/X64/EDK2/` | 工具包；**拷入时删除其中 `.git` / 子模块元数据**，不保留独立仓史 |
| `ToyKernel/` | `Kernel/` | 去 Toy 前缀 |
| `ToyImage/` | `Runtime/` | 名已定 |
| 顶层 `Build/` | **删（逻辑）** | → `Boot/<Arch>/Build/`、`Kernel/Build/` |
| 顶层 `Scripts/` | **拆尽** | 编 Boot→Boot 侧；编/跑 Kernel→Kernel 侧；刷盘/QEMU→Runtime 或 Kernel 约定脚本 |
| 顶层 `Config.txt` | **删除** | 默认进 `build.sh` / `env` 类脚本 |
| `ToyKernel/Documents/**` | **不搬** | 新树文档从零写；旧仓文档仍权威到你宣布切换 |
| `ToyKernel/OpenBox/`（旧开箱脚本树） | **参考源**，不整树拷 | 拆脚本时按需挑文件（仍先确认清单） |

此后 **只认单仓 OpenBox**。旧三仓（ToyBoot / ToyKernel / ToyImage）只读对照，不再往新树里嵌套第二套 `.git`。

---

## 3. Build

| 规则 | 说明 |
| ---- | ---- |
| 无顶层 Build | — |
| Boot | **`Boot/X64/Build/`**、**`Boot/Arm64/Build/`**、**`Boot/RiscV/Build/`**（按下沉；无统一 `Boot/Build/`） |
| Kernel | `Kernel/Build/` |
| Runtime | 只收同步结果；临时打包用 `Runtime/Staging/`，**不叫 Build** |

旧 `BUILDDIR` / sync 脚本：迁代码刀里改；旧仓暂不动。

---

## 4. 文档策略（已确认：零迁移）

| 原则 | 说明 |
| ---- | ---- |
| 旧仓文档 | 仍在 `~/ToyOS/ToyKernel/Documents`，对照用 |
| 新树文档 | **从新写**；先有根 README（短），其余随代码柱补 |
| 不搬 | 路线图、技术手册、待做、已完… **一律不拷进 OpenBox** |
| Boot 长文 | 需要时在 `Boot/` 下新写短 README，不从旧 Documents 抽迁 |

---

## 5. 三目录内部（边界；细部后接）

| 柱 | 本轮钉死 | 后接 |
| -- | -------- | ---- |
| Boot | `X64` / `Arm64` / `RiscV`；EDK2∈X64 | 驱动/固件细节 |
| Kernel | 源码树整体迁入后再谈 Hal/… 改名 | 四层驱动、可读性 |
| Runtime | 沿今日 Image 大意（RootFs/Store/…） | 同步脚本落点 |

**本轮不改名 CodeA、不搬驱动层。**

---

## 6. 迁移阶段（修订）

| 阶段 | 做什么 | 状态 |
| ---- | ------ | ---- |
| **M0** | 空骨架 + 本文 | ✅ 改落 `~/OpenBox` |
| **M1** | 拍板 §0 / §7 | ✅ 2026-10-07 |
| **M2** | ~~迁文档~~ **取消** | 文档新写，不单开迁文档刀 |
| **M3** | 迁 **Kernel** 源码 + `Kernel/Build` + 相关脚本（清单确认后） | 下一代码刀候选 |
| **M4a** | 迁 **Boot/X64**（原 ToyBoot）+ 裁剪 **EDK2** + `build.sh` 可编 | ✅ 待你 review（OpenBox） |
| **M4b** | Arm64 / RiscV 引导 | 分刀 |
| **M5** | 迁 **Runtime**（原 ToyImage）；跑盘/同步脚本 | 分刀 |
| **M6** | 旧 `~/ToyOS` 归档或切默认工作区 | 新树可编可跑后 |

### 铁律

1. **确认前**：不向 `~/OpenBox` 拷大树源码/ELF；旧仓零改名。  
2. **一刀一事**：按 Boot / Kernel / Runtime 分柱；每刀拟清单找你确认。  
3. **可回退**：旧 `~/ToyOS` 留到 smoke 过。  
4. **权威**：代码迁完前，**可运行权威仍在旧仓**；新树文档从薄到厚。  
5. **逐步 + review**：每刀迁完你 code review；未点头不开下一刀；禁止「一口气搬完再审」。

### 当前磁盘（节选）

```text
~/OpenBox/Boot/
  Include/BootInfo.h
  X64/{…, EDK2/, Build/BOOTX64.EFI}
  Arm64/{Boot.S, Boot.c, Build/*.o}
  RiscV/{…, Build/*.o}
```

Boot 说明写在真树 **`~/OpenBox/Boot/README.md`**（分架构同目录下）。（旧设想目录名 `ToyOSNew` 废弃。）

---

## 7. 拍板结果（原待决 → 已定）

| # | 议题 | 决定 |
| - | ---- | ---- |
| 1 | Runtime 名 | **采用 Runtime** |
| 2 | EDK2 | **`Boot/X64/EDK2/`**，与 ToyBoot 源码平级，工具包；**不保留独立 `.git`** |
| 3 | Config.txt | **删**；默认进 sh |
| 4 | Scripts | **全拆**，根不留 |
| 5 | git | **GitHub 新仓 `OpenBox`（此后唯一仓）**；旧 ToyBoot/ToyKernel/ToyImage 三仓仅对照，不再作为新树权威 |
| 6 | 文档 | **零搬**；结构确认后搬代码，文档新写 |
| 7 | 下一刀 | **不先迁文档**；目录确认后开始**搬代码**（仍每刀确认清单） |
| 8 | 工作区路径 | **`~/OpenBox`**，不用 ToyOSNew |

---

## 8. 原文（结构需求）

> 1. ToyOS 还是作为根目录，下面三个子目录 Boot，Kernel，Runtime；  
> 2. Build 回归 Boot 和 Kernel，顶层 Build 没了；  
> 3. Documents 拆开，根只有 README；  
> 4. 当前结构先不动，home 下新建再逐步迁移。

本轮补充：仓名/目录 **OpenBox**；Boot 三架构；EDK2∈X64；无 Config；Scripts 全拆；文档不搬。

---

## 9. 修订记录

| 日期 | 说明 |
| ---- | ---- |
| 2026-10-06 | M0：ToyOSNew 骨架 + 本文初稿 |
| 2026-10-07 | M1 拍板：改 `~/OpenBox`；Runtime/EDK2/无 Config/Scripts 全拆/单仓/文档零迁；取消 M2 迁文档 |
| 2026-10-07 | 单仓钉死：EDK2 拷入剥 `.git`，不嵌套第二仓 |
| 2026-10-07 | 代码逐步迁；每刀你 code review 后再开下一刀 |
| 2026-10-07 | Boot 产物按下沉：`Boot/<Arch>/Build/`；细则在 `~/OpenBox/Boot/README.md` |
