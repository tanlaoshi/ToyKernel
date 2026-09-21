# 任务：拆分 FilesUi.c（PR-S-filesui-split-1 / -2 / -3）

> **规格同 [`大文件拆分.md`](大文件拆分.md)**：只搬家、不改逻辑；`FilesUi.h` 不动；每刀 build + `smoke-boot`。  
> **本文件 = 轨 D · FilesUi 细则**；总盘点见 [`大文件拆分3.md`](大文件拆分3.md)。  
> **★ 柱完成**：三刀均已落地（split-3 TG `1907c97`）。  
> **统计时点**：2026-09-16；split-1/2/3 已落地。

### ★ 拆分进度

| 状态 | PR | 内容 | 说明 |
| --- | --- | --- | --- |
| ✅ TG `b3825db` | **PR-S-filesui-split-1** | `Include/FilesUiPrivate.h` + `FilesUiPaint.c` | Paint* 迁出；`FilesUiStrEqIgnoreCase` |
| ✅ TG `a2bb774` | **PR-S-filesui-split-2** | `FilesUiNav.c` | Bookmark*/Goto/Reload/Preview |
| ✅ TG `1907c97` | **PR-S-filesui-split-3** | `FilesUiActions.c` | Open/Delete/Prompt |

**行数（约）**：`FilesUi.c` ~575；`FilesUiPaint.c` ~469；`FilesUiNav.c` ~169；`FilesUiActions.c` ~168。  
**验收**：split-3 `./build.sh` + `smoke-boot` PASS（2026-09-16）。

---

## 一、背景

`Common/Services/FilesUi.c` 约 1411 行，职责混杂：工具、书签导航、绘制、写动作、事件入口、生命周期。按职责拆成 **4×`.c` + 1×内部头**。

---

## 二、硬约束

1. **只搬家，不改逻辑**；禁止顺手修 bug。  
2. **不改对外 API**：`FilesUi.h` 完全不动。  
3. **不改宏值 / 结构体布局**：`FILES_MODE` / `FILES_PROMPT_KIND` / `FILES_BOOKMARK` / `PREV_KIND` 等。  
4. **全局定义全部留在 `FilesUi.c`（宿主）**；其它 `.c` 经 `FilesUiPrivate.h` `extern`。  
   - 现为文件内 `static`；跨 TU 后改为**去掉 static** 的文件作用域定义（语义仍仅 FilesUi 族使用）。  
5. 每 PR 可编译 + smoke；`Makefile` 已 `wildcard Common/Services/*.c`，**不必改**。  
6. 原 `static` 跨文件后去掉 static，在 Private.h 声明；**命名保持原样**（不加前缀）。

### 2.1 Private.h 修正点（相对初稿）

| 项 | 初稿 | 修订 |
| --- | --- | --- |
| `FILES_BOOKMARK_COUNT` | `#define … sizeof(gBookmarks)` | **`#define FILES_BOOKMARK_COUNT 4`** |
| `StrEqIgnoreCase` | 保持原名 | **改为 `FilesUiStrEqIgnoreCase`**（与 `FatPath.c` / `FatPrivate.h` 全局符号冲突） |
| 宿主全局 | `extern` 清单 | 与现 `static` 清单一致；**无清单外全局** |
| include | 所列头文件 | 与现 `FilesUi.c` 一致即可（`Fat` 类型经 `FileSystem.h`/`FilesUi.h` 链入） |

---

## 三、目标结构

```
Common/Services/
├── FilesUi.c              # 工具 + 事件 + 生命周期 + 全部全局定义
├── FilesUiPaint.c         # Paint*（第 1 刀）
├── FilesUiNav.c           # 路径/书签/Reload/预览（第 2 刀）
├── FilesUiActions.c       # Open/Delete/Prompt（第 3 刀）
Include/
└── FilesUiPrivate.h          # 内部共享（第 1 刀一并创建）
```

---

## 四、分析结论（动手前核对 · 2026-09-16）

### 4.1 函数行数（与清单一致）

| 归入 | 函数 | 行数 |
| --- | --- | ---: |
| **Paint** | `PaintOverlay` | 47 |
| | `PaintList` | **274** |
| | `PaintView` | 76 |
| | `PaintConfirm` | 23 |
| | `PaintPrompt` | 22 |
| | `Paint` | 14 |
| | **小计** | **~456** |
| **Nav** | `BookmarkMatches` | 42 |
| | `SyncSideSel` | 10 |
| | `GotoPath` | 7 |
| | `SideHitIndex` | 18 |
| | `ReloadList` | 21 |
| | `IsMostlyText` | 15 |
| | `UpdatePreview` | 39 |
| | **小计** | **~152** |
| **Actions** | `OpenSelected` | 56 |
| | `BeginConfirmDelete` | 14 |
| | `BeginPrompt` | 22 |
| | `DoDelete` | 26 |
| | `DoPromptCommit` | 38 |
| | **小计** | **~156** |
| **Util（留 FilesUi.c）** | 11 个工具 | **~131** |
| **Events** | 9×`FilesUiOn*` | **~308** |
| **Life** | Open/Repaint/PaintFocused/Refresh/IsFocused | **~48** |

预估拆后：`FilesUiPaint.c` ≈ 480～550（含头）；`FilesUiNav.c` ≈ 180～220；`FilesUiActions.c` ≈ 180～220；宿主 `FilesUi.c` 三刀后 ≈ 550～650（含全局/事件；可略超「≤500」目标，以事件不二次拆为前提）。

### 4.2 有无遗漏？

**无。** 43 个函数均落在清单内；`extra = []`。

### 4.3 跨职责 / 难归类？

| 函数 | 备注 |
| --- | --- |
| `IsMostlyText` / `UpdatePreview` | 偏「预览」；放 **Nav** 合理（Reload→UpdatePreview 同文件） |
| `DrawLine` | 绘制小工具；留 **FilesUi.c**，Paint 经 Priv 调用 |
| `PaintList` | 单函数 274 行；本柱**不**再拆列表内部 |

无必须改归类的硬冲突。

### 4.4 全局变量（相对清单）

清单内外一致。现有全部为 `static`：

`gCwd` / `gEnts` / `gCount` / `gSelected` / `gScroll` / `gMode` / `gView` / `gViewLen` / `gViewTitle` / `gPromptKind` / `gPrompt` / `gPromptLen` / `gStatus` / `gClick*` / `gHoverIdx` / `gSideHover` / `gSideSel` / `gBookmarks[]` / 滚动条与侧栏几何 / `gPrev*` / `gPrevKind`。

**无清单外全局。**

### 4.5 前向声明如何处理？

现 L304–306：

```c
static int ReloadList(void);
static void Paint(void);
static void UpdatePreview(void);
```

| 刀 | 处理 |
| --- | --- |
| **split-1**（Paint 迁出） | 删 `static void Paint(void);`；`Paint` 由 Private.h 声明；宿主事件仍调 `Paint()` |
| **split-2**（Nav 迁出） | 删 `ReloadList` / `UpdatePreview` 前向声明；改由 Private.h |
| 三刀前 | Nav 仍在同文件时，前向声明可暂时保留到 split-2 |

### 4.6 跨文件调用（须 Private.h）

| 调用方 | 被调 |
| --- | --- |
| FilesUi.c（事件/Life） | `Paint` / `PaintList`…；Nav；Actions |
| FilesUiPaint.c | 全局 + Util（`DrawLine`/`CopyStr`/…） |
| FilesUiNav.c | 全局 + Util；`UpdatePreview`↔`ReloadList` 同文件 |
| FilesUiActions.c | 全局 + Util + 可能 `ReloadList`/`Paint`（经 Priv） |

对外仍只导出 `FilesUi.h` 中的 `FilesUi*`。

---

## 五、FilesUiPrivate.h（第 1 刀创建）

路径：`Include/FilesUiPrivate.h`。内容以用户规格为准，并应用 **§2.1**（`FILES_BOOKMARK_COUNT` 改为字面 **4**）。

禁止 User / HAL / Core 包含。

---

## 六、分刀策略（★ 第 1 刀优先）

**不要一次拆完。** 每刀独立编译 + smoke：

### 第 1 刀 — **PR-S-filesui-split-1** ✅ TG `b3825db`

1. 创建 `Include/FilesUiPrivate.h`（宏/类型/全部全局 extern/全部内部函数声明）。  
2. 创建 `Common/Services/FilesUiPaint.c`：搬 `PaintOverlay` / `PaintList` / `PaintView` / `PaintConfirm` / `PaintPrompt` / `Paint`，去 `static`。  
3. `FilesUi.c`：删已搬 Paint*；宏/类型可先留在 `.c` **或** 已迁 Priv 则删重复；全局仍 `static`→改为非 static 定义以便其它 TU `extern`（**第 1 刀就必须去掉这些全局的 static**，否则 Paint.c 链不上）。  
4. 宿主顶部改为 `#include "FilesUiPrivate.h"`。  
5. `./build.sh` + `ToyImage/smoke-boot.sh`；QEMU 开 Files：列表/预览/侧栏**显示**正常。

**第 1 刀故意不做**：不建 Nav/Actions；不改写操作逻辑。

### 第 2 刀 — **PR-S-filesui-split-2** ✅ TG `a2bb774`

`FilesUiNav.c`：Bookmark* / GotoPath / SideHit / ReloadList / IsMostlyText / UpdatePreview。  
验收：build + smoke-boot PASS（进目录/预览/书签待人工点验）。

### 第 3 刀 — **PR-S-filesui-split-3** ✅ TG `1907c97`

`FilesUiActions.c`：OpenSelected / BeginConfirmDelete / BeginPrompt / DoDelete / DoPromptCommit。  
验收：build + smoke-boot PASS（打开/删/建/改名待人工点验）。

---

## 七、验收标准（柱完成时）

- [x] `FilesUiPaint.c` ≤ 700；`FilesUiNav.c` / `FilesUiActions.c` ≤ 350  
- [x] 宿主尽量 ≤ 600（事件多时可略超；现 ~575）  
- [x] 编译无新增警告；`smoke-boot` PASS  
- [ ] QEMU Files：开窗、进目录、预览、滚动、悬停、删/建/改名、Esc  
- [ ] 建议 NUC 点验 Files 不回归  

---

## 八、与总计划关系

[`大文件拆分3.md`](大文件拆分3.md) 轨 D：**第 1 刀改为 FilesUi-split-1**（先于 Desktop 拆分）。Desktop 顺延。
