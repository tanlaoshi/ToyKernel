# ToyOS 大目录结构（ToyOSNew · 新思路）

> **状态**：草案。只写结构与迁移规矩；**确认前不动旧仓、不往新树搬源码**。  
> **工作区**：迁移目标 `~/ToyOSNew`；现网 `~/ToyOS` 只读对照。  
> **与旧草案关系**：[`目录树与可读性方案.md`](目录树与可读性方案.md) 里「根级 `10-Boot`/`30-Build`」等顶层设想**被本文取代**。分层可读、四层驱动（Class→Protocol→Family→Chip）、人话头等**细部仍可沿用**，待大树钉死后再接。

---

## 0. 你定下的硬规矩（本轮）

| # | 规矩 |
| - | ---- |
| 1 | 根仍叫逻辑名 **ToyOS**；下面**三个**子目录：**Boot**、**Kernel**、**Runtime**（相对旧名要改名） |
| 2 | **取消**顶层 `Build/`；构建产物分别回到 **Boot/Build**、**Kernel/Build** |
| 3 | **Documents 拆开**：ToyOS 根**只有** `README.md`；文档大部分进 **Kernel**，少部分进 **Boot** |
| 4 | **现仓结构先不动**；在 `~/ToyOSNew` 建新树，**逐步迁移**；先文档、确认后再动手 |

---

## 1. 目标顶层（人话）

打开 `ToyOS`（迁移完成后可由 `ToyOSNew` 改名/替换）应只看到：

```text
ToyOS/
  README.md          # 唯一总入口说明（地图 + 怎么编/跑的一页）
  Boot/              # 引导：源码 + Boot/Build/
  Kernel/            # 内核/用户态：源码 + 文档大部 + Kernel/Build/
  Runtime/           # 运行镜像：RootFs、ESP、种子…（无顶层 Build）
```

| 目录 | 阶段 | 人话 |
| ---- | ---- | ---- |
| Boot | 开发 · 引导 | 机器怎么把 Kernel 拉起来 |
| Kernel | 开发 · 系统 | OS 本身怎么写、怎么编 |
| Runtime | 运行 | 编出来的东西怎么摆盘、怎么跑 |

字母序：`Boot` → `Kernel` → `Runtime` →（若根上只有这三夹 + README，浏览顺序即故事线）。  
**不再**出现顶层 `Build` 抢在 Boot 前面。

---

## 2. 与旧仓对照（改名表）

| 旧路径（`~/ToyOS`） | 新路径（`~/ToyOSNew` → 将来的 ToyOS） | 备注 |
| ------------------- | ------------------------------------- | ---- |
| `ToyBoot/` | `Boot/` | 去 Toy 前缀 |
| `ToyKernel/` | `Kernel/` | 去 Toy 前缀 |
| `ToyImage/` | `Runtime/` | 「镜像/运行料」比 Image 更贴人话；若你更想叫 `Image` 可再拍板 |
| `Build/`（顶层） | **删除（逻辑上）** | 拆进 `Boot/Build/`、`Kernel/Build/` |
| `ToyKernel/Documents/` | `Kernel/Documents/` | 大部分文档 |
| （引导相关少量文档） | `Boot/Documents/` 或 `Boot/README` + 短文 | 见 §4 |
| 根 `README`（若无） | `README.md` | 根唯一长说明入口 |
| `Config.txt` | **待决**：根 / `Kernel/` / 各侧一份 | 见 §7 |
| `Scripts/` | **待决**：根不要 Scripts 大院时 → 拆进 Boot/Kernel | 见 §7 |
| `EDK2/` | **待决**：常跟 Boot 走 → `Boot/EDK2` 或 `Boot/ThirdParty/EDK2` | 见 §7 |
| `.cursor/` | 随工作区；可不进 Runtime | 工具配置 |

多仓 git（ToyBoot / ToyKernel / ToyImage）是否合成一个 `ToyOS` 仓：**另议**；物理上可仍三仓，逻辑上按上表对齐路径。

---

## 3. Build 回归两侧

### 3.1 规则

| 规则 | 说明 |
| ---- | ---- |
| 无顶层 Build | `ToyOS/Build` 不再作为共享大院 |
| Boot 自产自消 | `Boot/Build/`：EFI、引导中间文件等 |
| Kernel 自产自消 | `Kernel/Build/`：`Kernel.elf`、各 arch 目标、测试宿主二进制等 |
| Runtime 不编译内核 | Runtime **接收**拷贝/同步结果（ELF、资源），不设「第三套编译树」；若需临时打包目录，用 `Runtime/Staging/` 一类名，**不叫 Build** |

### 3.2 旧路径迁移示意

| 今日 | 明日 |
| ---- | ---- |
| `ToyOS/Build/ToyKernel/...` | `Kernel/Build/...` |
| （若有）Boot 产物混在顶层 Build | `Boot/Build/...` |
| `ToyImage/RootFs/.../Kernel.elf`（同步目标） | 仍在 `Runtime/...`；来源改为从 `Kernel/Build` 拷 |

`build.sh` / Makefile 里的 `BUILDDIR`、同步到 Image 的脚本：迁移刀里改路径；**本阶段只记债，不改旧仓**。

---

## 4. Documents 怎么拆

### 4.1 根

- **仅** `README.md`：三目录地图、一键「去哪读文档 / 怎么编 Boot / 怎么编 Kernel / 怎么跑 Runtime」。  
- **不**放路线图、不放驱动长文、不放进展日记。

### 4.2 进 Kernel（默认：绝大部分）

凡与内核、用户态、驱动框架、桌面、商店、开课 ABI、可读性、路线图、待做进展相关：

```text
Kernel/Documents/
  开发/
  驱动/
  待做/
  已完/
  路线图.md
  技术手册.md
  …
```

即今日 `ToyKernel/Documents/**` 的主体原样迁入（路径去 `Toy` 前缀即可）。

### 4.3 进 Boot（少量）

只放**引导专用、Kernel 读者不必先读**的短文，例如：

| 宜进 Boot | 例子（名义） |
| --------- | ------------ |
| 如何编 Boot / 如何打 EFI | `Boot/Documents/如何构建Boot.md` |
| 与 EDK2/OVMF 交接 | 引导 Handoff、固件变量 |
| 板级 / 架构引导差异 | `Boot/Documents/Arch-X64.md` 等 |

宜留在 Kernel 的：开机**内核侧**流程（`开机流程与加速.md`）、HAL 进内核之后的故事——即使提到 Boot，也以 Kernel 视角写，Boot 侧用链接指过去。

### 4.4 原则

| 原则 | 说明 |
| ---- | ---- |
| 一份权威 | 同一主题不在 Boot/Kernel 各写一篇长文；一侧权威、一侧链接 |
| 根不藏书 | 根 README 只做索引 |
| 迁移时带链接债 | 旧文互相引用路径批量替换：`ToyKernel/Documents` → `Kernel/Documents` 等 |

---

## 5. 三目录内部（本轮只定边界，细部后接）

确认大树后，再把旧「目录树与可读性方案」里的下列块接进来（可另文或本文续节）：

- Boot：按 **X64 / Arm64 / RiscV** 分架构夹  
- Kernel：`Hal` / `Library` / `Core` / `Services` / `User` + 数字前缀排序；驱动四层 Class→Protocol→Family→Chip  
- Runtime：`Esp` / `RootFs` / `Store` / …（大致沿今日 ToyImage）

**本轮不展开改名 CodeA、不搬驱动。**

---

## 6. 迁移策略（ToyOSNew）

### 6.1 阶段

| 阶段 | 做什么 | 何时 |
| ---- | ------ | ---- |
| **M0** | 本文 + `ToyOSNew/{Boot,Kernel,Runtime}` 空骨架 + 根 README | **现在（已做）** |
| **M1** | 你确认本文 §0～§4、§7 待决 | 确认前 **停** |
| **M2** | 迁文档：Kernel/Documents 主体；Boot 少量；改根 README 索引 | 确认后第一刀 |
| **M3** | 迁 Kernel 源码与 `Kernel/Build` 约定；脚本改 `BUILDDIR` | 分多刀 |
| **M4** | 迁 Boot 源码与 `Boot/Build`；安置 EDK2 | 分多刀 |
| **M5** | 迁 Runtime（原 ToyImage）；同步脚本改源路径 | 分多刀 |
| **M6** | 旧 `~/ToyOS` 标只读/归档或切换默认工作区 | 全新树可编可跑之后 |

### 6.2 铁律

1. **确认前**：不向 `ToyOSNew` 拷 ELF/源码大树；旧仓零改名。  
2. **一刀一事**：文档刀与代码刀分开；代码刀按 Boot / Kernel / Runtime 分柱。  
3. **可回退**：旧 `~/ToyOS` 保留到新树 smoke 通过。  
4. **双树并存期**：文档写明「权威在旧仓还是新仓」——迁文档刀完成前，**权威仍在旧仓**。

### 6.3 当前磁盘状态（M0）

```text
~/ToyOSNew/
  README.md
  Boot/README.md
  Kernel/Documents/开发/目录结构-ToyOSNew.md   ← 本文
  Runtime/README.md
```

---

## 7. 待你拍板

1. **Runtime** 这个名字是否最终采用，还是改回 `Image` / 改用 `Run`？  
2. **EDK2**：`Boot/EDK2` 还是 `Boot/ThirdParty/EDK2`？  
3. **根 Config.txt**：留根 / 进 Kernel / Boot·Kernel 各一份？  
4. **Scripts**：全部拆进 Boot/Kernel，还是允许根上暂时保留 `Scripts/`（与「根只有 README」略冲突）？  
5. **git**：三仓继续，路径逻辑对齐；还是新树单仓？  
6. **Boot 文档最小集**：列 3～5 个文件名你点头后，其余一律进 Kernel。  
7. 确认后是否同意：**下一刀只做 M2（文档迁移）**，仍不搬 `.c`？

---

## 8. 原文（本轮需求）

> 现在我想换一套思路，先整理大的文件结构，你重新写一个文档，  
> 1. ToyOS 还是作为根目录，下面三个子目录 Boot，Kernel，Runtime，这要改名字；  
> 2. 原来的 Build 还是回归到 Boot 和 Kernel 下，Build 目录就没了；  
> 3. Documents 拆开，ToyOS 下只有一个 README.MD，其余大部分文件进 Kernel，少部分进 Boot；  
> 4. 当前目录的结构先不动，你在 home 下建一个 ToyOSNew，逐步迁移过去，先写文档，确认好后再动手。

---

## 9. 修订记录

| 日期 | 说明 |
| ---- | ---- |
| 2026-10-06 | M0：ToyOSNew 骨架 + 本文；旧仓不碰 |
