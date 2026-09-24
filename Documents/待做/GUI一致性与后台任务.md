# ToyOS GUI 一致性 + 按钮事件 + 后台任务模型

> **状态**：规格**草案**（2026-09-24）；**代码未动**；待用户确认后做第 1 刀。  
> **前置**：无强依赖；可与设备管理器阶段 3 并行（不抢 ★）。  
> **目标**：统一按钮外观（Theme + 控件层）、统一按钮行为（点击 → 事件 → 分发）、慢操作交给后台、评估进程模型。  
> **相关**：`Include/UI.h` · `Common/Library/UI.c` · `Include/Theme.h` · `Include/Scheduler.h` · `Include/StoreJob.h`  
> **命名**：PascalCase；新 `.c` ≤300；三架构可编。

---

## 〇、已确认决策（写死 · 待用户确认）

| # | 决策 | 结论 |
| - | ---- | ---- |
| 1 | 控件层位置 | **新建** `Include/UiButton.h` + `Common/Library/UiButton.c`（widget 层）；`UI.c` 保留为**图元层**。`UiButtonDraw` 内部调 `UiDrawButtonEx`，**不**重写绘制 |
| 2 | 按钮态 | 4 态：NORMAL / HOVER / PRESSED / DISABLED。`UiDrawButtonEx` 现仅 3 态，**扩展** DISABLED |
| 3 | Theme 按钮色 | 新增 `ThemeButtonFace/Border/Text` × {Normal,Hover,Pressed,Disabled}；先**只追加 getter**，不进 CFG/DB |
| 4 | 事件分发 | `UiAction.h` + `Common/Library/UiAction.c`；`UI_BUTTON_ACTION` + `UiActionDispatch`；**一次迁一页** |
| 5 | 后台任务 | Store **已有** `WorkerTask`+`StoreJob`。本柱**不重写 Store**；只抽一个**通用 `KernelTask` 注册表** + `SchedulerCreateKernel(Name,Fn,Ctx)`，供**未来**慢操作用 |
| 6 | 不引入线程 | 共享地址空间的内核任务足够；不引入 vtable / 完整线程 |
| 7 | 第 1 刀验证页 | **EditUi**（单 Save 钮，最小）——**已确认**（2026-09-24） |

实现时以上条目不得再议；若要改，先改本文再动代码。

---

## 一、目标与硬约束

### 目标

1. 所有按钮从 Theme 取色、同一套绘制 + 同一套 4 态状态机。
2. 页面层只声明「按钮 + 动作」；press→release 命中由统一分发处理。
3. 慢操作有**通用**后台模型可走（Store 已自带，本柱补一个通用层给将来）。
4. 评估当前进程模型——**结论：够用**（见 §3.3）。

### 硬约束

| # | 约束 |
| - | ---- |
| 1 | **不改**现有 syscall 号 |
| 2 | **不改**现有 API 签名（`UiDrawButton`/`UiDrawButtonEx` 旧签名保留；只追加） |
| 3 | **不改**现有点击行为（迁移后动作与今一致） |
| 4 | **一次迁移一个页面**；每刀 build + smoke |
| 5 | 新 `.c` ≤300；三架构可编 |
| 6 | **不**引入完整线程 / vtable |
| 7 | **不**破坏现有同步操作（EditUi Save / FilesUi Delete 仍同步——FAT 快，不值当后台） |

### 明确不做

- ❌ 一次迁移所有页面
- ❌ 重写 Store 的 WorkerTask/StoreJob（已工作；只抽通用层）
- ❌ 把 EditUi Save / FilesUi Delete 改成异步（FAT 同步够快）
- ❌ 引入线程 / vtable / 控件树

---

## 二、现状锚点（分析 · 2026-09-24）

### 2.1 图元层 `Common/Library/UI.c`（490 行）

已有：
- `UiDrawButton(X,Y,W,H,Text,TextColor,BgColor)` — 无态按钮（EditUi 在用）
- `UiDrawButtonEx(...,Hovered,Pressed)` — **3 态**（normal/hover/pressed），圆角 5px、`ThemeControlBorder`/`ThemeControlAccent`、按下凹陷 1px（StoreUi 在用）
- `UiHitRect`、`UiDrawProgressBar`、`UiDrawListRow`、`UiDrawScrollBar`、`UiScrollBarHit`、`UiListRowFromY`

**缺**：DISABLED 态；**没有** widget 结构体（即时模式，无控件树）；**没有** press→release 状态机（各页自己写）。

### 2.2 各页按钮绘制（**不一致**）

| 页 | 绘制方式 | 态 |
| -- | ------- | -- |
| StoreUi | `UiDrawButtonEx` | 3 态（hover/press）✓ |
| EditUi | `UiDrawButton` | **无态**（无 hover） |
| SettingsUi | `UiDrawListRow`（列表行，非按钮） | 选中/悬停 |
| DevicesUi | **手写** `HalVideoFillRect`+`HalVideoDrawStringAt` | 无态 |
| FilesUi | **手写** 对话框 `HalVideoFillRect` | 无态 |

→ 「外观不一致」**成立**：3 种画法并存。

### 2.3 各页事件分发（**各写一套**）

每页都自带 hit-rect 表 + press→release 状态机：

| 页 | hit 表 | 状态变量 | 触发 |
| -- | ------ | -------- | ---- |
| SettingsUi | `gSetHits[]`（Kind/Index） | `gSetHoverKind/Idx` `gSetPressKind/Idx` | `SelectCategory`/`ApplyItem` |
| StoreUi | 内联 `gBtnX0`+循环 | `gHoverBtn` `gPressBtn` | `DoBtn`→`StoreJobEnqueue` |
| FilesUi | `gSetHits`-like | 同上 | `DoDelete`/`FileSystemRename` |
| EditUi | `gSaveButtonHit` | 无 press 态 | Save→`FileSystemWriteFile` |
| DevicesUi | `OnClick` 内联 | 无 | 列表选择 |

→ 「事件逻辑各异」**成立**：5 套近似状态机，迁移/改态都要逐页改。

### 2.4 后台任务（**Store 已有**）

- `WorkerTask`（`Common/Services/Tasks/Tasks.c`，Kernel.c 创建为 `"worker"` 内核任务）：循环 `StoreJobStep()` + `SchedulerIoBreath()`。
- `StoreJob`（`Common/Services/StoreUi/StoreJob.c`，321 行）：`StoreJobEnqueue(Kind,Id)` 入队即返回；`StoreJobStep` 推进状态机；`StoreJobIsBusy/IsRunning/Cancel`；`StoreJobStatusProgress` 写进度文案。
- UI（`StoreUiPump`）**刻意不 Step**——注释明说：旧路径在 GuiPollMouse 里 Step 会占死 Gui，装卸期鼠标必卡。**这是已验证的后台模型**。

→ 用户前提「安装/下载会卡住 UI」**对 Store 已过时**；Store 早已后台化。真正还同步的慢操作只有 FilesUi Delete / EditUi Save（FAT，快）。

### 2.5 进程/任务模型

| 能力 | 现状 |
| -- | ---- |
| 任务上限 | `MAX_TASKS 16`（非 8） |
| 调度 | 抢占式（`SchedulerOnTimer`）+ 协作让步（`SchedulerCondResched` / `SchedulerIoBreath`） |
| 优先级 | `TASK.Priority`（shell/gui=8，idle=-128） |
| 内核任务 | `SchedulerCreate(Name, void (*Entry)(void))` — **无 Ctx 参数** |
| 用户进程 | `SchedulerCreateUser(...)` |
| 线程 | 无 |
| 已有内核任务 | shell / gui / worker / input（/ idle） |

**关键缺口**：`SchedulerCreate` 的 entry 是 `void(*)(void)`，**无上下文指针**。StoreJob 靠**静态全局**（`sKind`/`sId`/...）传参。要做「一任务一上下文」的通用内核任务，需加 `SchedulerCreateKernel(Name, Fn, Ctx)`（entry 收 `void*`）。

---

## 三、Agent 思考（相对任务书校正）

### 3.1 不要照抄任务书的 `UiButton.c` 重写绘制

任务书 `UiButtonDraw` 直接调 `HalVideoFillRect`/`HalVideoDrawRect`/`HalVideoDrawStringAt`——会与现有 `UiDrawButtonEx`（圆角、凹陷、Theme 边色）**重复且退化**（丢圆角）。`HalVideoDrawRect` 本仓**不存在**，应用 `UiDrawRectangle`。

**校正**：`UiButton.c` 是 **widget 层**（结构体 + 状态机 + 命中），绘制**委托** `UiDrawButtonEx`（扩展 DISABLED）。

### 3.2 后台任务：Store 已解决，本柱只抽通用层

任务书第 3 部分假设「慢操作卡 UI」——Store 已用 `WorkerTask`+`StoreJob` 解决。**不重写 Store**。本柱第 3 刀只做：

- `KernelTask.h`/`Common/Core/KernelTask.c`：通用注册表（`Name/State/Progress/Message` + `Fn/Ctx`），≤4 槽。
- `Scheduler.h`/`Scheduler.c`：加 `SchedulerCreateKernel(Name, void (*Fn)(void*), void *Ctx)`；现有 `SchedulerCreate` 不动。
- **不**强制 Store 改用；留 demo/未来慢操作用。Store 迁移**另柱**（可选）。

### 3.3 进程模型结论：够用，不需线程

| 需求 | 现模型 | 线程 |
| -- | ------ | ---- |
| 慢操作不卡 UI | ✅（WorkerTask 已证） | ✅ |
| 共享状态 | ✅（内核任务共享地址空间） | ✅ |
| 复杂度 | 低 | 高 |
| 调度器改动 | 小（加 Ctx 变体） | 大 |

→ **不引入完整线程**。内核任务（共享地址空间 + 独立栈）足够，本质是轻量线程。

### 3.4 第 1 刀验证页选谁

| 候选 | 利 | 弊 |
| ---- | -- | -- |
| **EditUi**（推荐） | 单 Save 钮、最小改动、易证 4 态 + 分发 | 课体感弱 |
| StoreUi | 钮最多（Install/Remove/Sync/Cancel）、已异步 | 改动大、易回归 |
| SettingsUi | 列表行非按钮 | 不典型 |

**建议**：第 1 刀迁 **EditUi**（最小验证）；第 2 刀迁 StoreUi（最丰富）；其余按需。

---

## 四、PR 拆分（5 刀）

| 序 | PR（建议名） | 交付 | 验收 |
| -- | ------------ | ---- | ---- |
| **1** | **PR-GUI-btn-widget** | `UiButton.h`/`UiButton.c`（`UI_BUTTON` + 4 态 + `UiButtonDraw/Hit/OnClick`）；`UiDrawButtonEx` 扩 DISABLED；Theme 加按钮 4 态色 | build + smoke；EditUi 仍旧貌（未迁） |
| **2** | **PR-GUI-btn-action** | `UiAction.h`/`UiAction.c`（`UI_BUTTON_ACTION` + `UiActionDispatch`，SYNC/ASYNC 两型） | build + smoke；桩证明 dispatch 命中 |
| **3** | **PR-GUI-migrate-edit** | 迁 **EditUi** Save 钮到 `UI_BUTTON_ACTION`（SYNC） | EditUi Save 行为不变；4 态可见 |
| **4** | **PR-GUI-migrate-store** | 迁 **StoreUi** 钮到 `UI_BUTTON_ACTION`（ASYNC 走 `StoreJobEnqueue`） | StoreUi Install/Remove/Sync 行为不变 |
| **5** | **PR-GUI-kerneltask** | `KernelTask.h`/`Common/Core/KernelTask.c` + `SchedulerCreateKernel(Name,Fn,Ctx)`；一个 demo 慢任务 | build + smoke；demo 任务跑完不卡 UI |

**依赖**：1 → 2 → 3 → 4；5 独立（可插在 2 之后或最后）。  
**勿**在刀 3/4 一次迁多页。  
**Store 迁移（刀 4）只换分发层**，`StoreJob`/`WorkerTask` 不动。

---

## 五、执行顺序

### 第 1 步：统一外观（刀 1）

1. 新增 `UiButton.h` / `UiButton.c`（widget 层，绘制委托 `UiDrawButtonEx`）
2. `UiDrawButtonEx` 扩 DISABLED 态（追加签名，旧签名保留）
3. Theme 加 `ThemeButtonFace/Border/Text` × 4 态 getter
4. build + smoke（不迁页）

### 第 2 步：统一事件分发（刀 2）

1. 新增 `UiAction.h` / `UiAction.c`（`UI_BUTTON_ACTION` + `UiActionDispatch`）
2. 桩证明 SYNC/ASYNC 命中
3. build + smoke

### 第 3 步：迁移验证页（刀 3 EditUi → 刀 4 StoreUi）

1. 迁 EditUi Save（SYNC）
2. 迁 StoreUi 钮（ASYNC 走 `StoreJobEnqueue`）
3. 每页 build + smoke，行为不变

### 第 4 步：通用后台任务（刀 5）

1. `KernelTask.h` / `Common/Core/KernelTask.c`
2. `SchedulerCreateKernel(Name, Fn, Ctx)`
3. demo 慢任务验证不卡 UI
4. Store **不**强制迁移

---

## 六、确认清单（已确认 · 2026-09-24）

- [x] 控件层新建 `UiButton.h/c`，绘制委托 `UiDrawButtonEx`（不重写）
- [x] 4 态含 DISABLED；Theme 加按钮色（只 getter）
- [x] 事件分发 `UiAction.h/c`；一次迁一页
- [x] Store 已有 Worker，**不重写**；刀 5 只抽通用 `KernelTask`
- [x] 不引入线程 / vtable
- [x] 第 1 刀验证页：**EditUi**（已选）
- [x] 5 刀顺序：1→2→3→4，5 独立
- [x] 排进路线图排队（GUI 控件与事件统一柱，不抢 ★）

**规格已确认；下一步做第 1 刀 `PR-GUI-btn-widget`。**
