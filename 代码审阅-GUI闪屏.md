# ToyKernel 代码审阅（侧重 GUI / 闪屏）

**日期**：2026-09-03（G8 落地后修订）  
**基线**：PR-D7 后；**PR-G6 ✅**；**PR-G7 ✅**；**PR-G8 ✅**（Theme 一次 Compose）  
**范围**：以 `Gui.c` / `Desktop.c` / `Theme.c` / `Console.c` / `SettingsUi.c` / `Video.c` 为主；顺带列出可改进点。  
**结论**：G6～G8 已修合成露底、长 cli、Theme 多遍闪屏；仍是 **单缓冲直写 GOP**；撕裂级闪屏待 **G9 backbuffer**。

---

## 0. 闪屏问题总览（优先读）

### 0.1 根因分层

| 层 | 问题 | 用户体感 | 状态 |
|----|------|----------|------|
| **呈现** | 直接写 scanout FB，无 backbuffer / vsync | 多段更新被扫描线看到 → 撕裂、灰闪 | → G9 |
| **同步** | 用长 `cli`（`HalIrqDisable`）冒充「帧原子性」 | USB 鼠标 IRQ 饿死 → 卡顿、跳变 | **G7 ✅** |
| **合成语义** | 起始 footprint under-drag / 关窗露底 | 拖开后下层文字抹掉 | **G6 ✅** |
| **更新策略** | Theme / 关窗走「破坏性重绘」而非同一套 Compose | 中间帧对用户可见 | **G8 ✅**（关窗客户区 G6 已用备份） |
| **光标/备份** | 开窗等路径未统一擦光标再备份 | 十字残影烙进窗备份 | **G7 ✅** |

已做好、应保留：整窗备份保留客户区文字；拖动脏区离屏一次 `WriteRect`；`GuiRedraw` 用 Raw 图标；**G6** under-drag 抓取时贴备份、合成采样含标签。

### 0.2 推荐目标架构（可渐进）

```
图层：Desktop（底色+图标） / WindowBackup[i] / Cursor
        ↓  合成可在开中断下进行
Compositor(脏矩形) → Backbuffer
        ↓  仅 Present 用短 GfxIrqEnter
Backbuffer → GOP（有条件再等 VBlank）
```

**IRQ 原则**：合成在 `sti`；只锁「光标 erase/paint」和「单次 Present」。禁止在 cli 下做 O(分辨率) 的 `DesktopDrawRect` / 全屏 `ReadRect`。

### 0.3 建议落地顺序

1. **H1** under-drag 改用窗备份（重叠拖动立刻好转）  
2. **H2** `CloseWindow` 用备份恢复相交窗客户区  
3. **H3** 缩短 cli：`MoveWindowTo` 只锁 Present；抓 snap 移出临界区；拖动中去掉或延后 `DesktopDrawRect`  
4. **H4** Theme：重绘到备份 → 一次 Compose，去掉 Shell 穿透写屏  
5. **H5** Backbuffer（消除撕裂）  
6. 统一光标 / `GfxIrq*`（M3 / M8）

---

## 1. High

### H1. under-drag 用解析像素盖掉下层真实内容 — **G6 ✅**

- **位置**：`Gui.c` `CaptureDragRestoreData` / `TopmostBelowDragPixel` / `CompositeDragPixel`  
- **落地**：抓 under-drag 时暂时 deactivate 拖窗 → 桌面 + `PaintWindowFromBackup` 相交窗 → `ReadRect`；合成优先 under-drag / screen snap / 窗备份；`AnalyticWindowPixel` 仅无备份回退。

### H2. 关窗只填桌面 + 重画 chrome，不恢复客户区 — **G6 ✅**

- **位置**：`Gui.c` `CloseWindow`  
- **落地**：相交窗 `PaintWindowFromBackup`。

### H3. 裸 `HalIrqDisable` 临界区过长 → USB 鼠标饿死 — **G7 ✅**

- **落地**：`MoveWindowTo` / `Capture` / `Sync` / `GuiRedraw` 合成开中断；仅光标与 `WriteRect` Present 走 `GfxIrq*`；`gComposeBusy` 丢弃嵌套鼠标；拖尾 `SyncWindowVisualsEx(1)` 清桌面残影。

### H4. ThemeApply 多遍全屏/多窗重绘，闪屏几乎必然 — **G8 ✅**

- **落地**：`GuiApplyThemeColors` 只改属性；`GuiComposeThemeScene` 自下而上 `DrawWindowAt` + Shell/Settings 内容后一次备份；标题逐字 occlusion（M4）；`ThemeSettingsClientBg`（M10）。

### H5. 无双缓冲 / 无 vsync

- **位置**：`Video.c` `VideoDrawPixel` / `FillRect` / `WriteRect`；GUI 最终落点  
- **问题**：关窗填色后再画 chrome、Theme 多遍、拖动行缓冲回退逐行写等中间状态会被扫描线看到。`gDragDirty` 一次写出只缓解拖动，不消除撕裂。  
- **方向**：与屏同尺寸（或脏矩形）backbuffer；Present 一次 blit；条件允许等 VBlank。

---

## 2. Medium

### M1. 合成后再 `DesktopDrawRect`：二次写屏 + 拉长 cli — **G6 部分 ✅**

- **落地**：`gDragHasBackup` 路径只 `CompositeDragDirtyRegion`，提交后不再 `DesktopDrawRect`；fallback 路径仍可能走 `ClearOldDragFootprint`。

### M2. `DesktopSamplePixel` 与真实绘制不一致 — **G6 ✅**

- **落地**：采样含标签字形，几何与 `DrawOneIcon*` 一致。

### M3. 开窗不隐藏光标、不走统一 IRQ 锁 — **G7 ✅**

- **落地**：`GuiOpenShell` / `GuiOpenSettings` 在 `DrawWindowAt`/`BackupWindowAt` 前 `GfxIrqEnter` + `CursorRestore`。

### M4. `DrawWindowAt` 标题无逐像素遮挡 — **G8 ✅**

- **落地**：`DrawTitleStringOccluded` 逐字 `PixelOccludedByAbove`。

### M5. Clip「名不副实」：绝对坐标绘制无视 clip

- **位置**：`Video.c`：clip 主要约束相对光标 `VideoDrawChar` / 滚屏；`FillRect` / `DrawStringAt` 不受裁  
- **问题**：Settings `SetClipRegion` 后仍 `DrawStringAt`，防写穿靠自觉。  
- **方向**：`DrawPixel`/`FillRect`/`DrawStringAt` 尊重 `gClipOn`；或文档写明 Settings 不依赖 clip。

### M6. 重叠时跳过被挡窗备份刷新 → 陈旧备份

- **位置**：`BeginDragBackups` / `StartDragBackups`  
- **问题**：被挡窗可保留旧 `gWinBackupValid`，合成贴旧图。  
- **方向**：拖动开始 sti 下对所有活动窗强制备份；或 raise / 内容变更时保证新鲜。

### M7. `DRAG_ROW_MAX` 回退静默截断脏区宽

- **位置**：`CompositeDragDirtyRegion` 无 `gDragDirty` 时  
- **问题**：`DuW` 截到行缓冲上限，右侧未更新 → 残影。  
- **方向**：预分配失败则拒绝备份拖动并回退；或分 tile 写，禁止静默截断。

### M8. 裸 `HalIrqEnable` 与 `GfxIrq*` 嵌套冲突 — **G7 ✅**

- **落地**：`Gui.c` 热点路径仅经 `GfxIrqEnter/Leave`（保存 IF、可嵌套）。

### M9. 换字体后布局变、中间写穿可能进备份

- **位置**：`ThemeApply` 顺序  
- **问题**：最终有 `GuiBackupAllWindows`，但中间 Shell 穿透写屏若被备份则拖动带脏像素。  
- **方向**：先画对（occlusion / 只写备份）再 backup。

### M10. Settings 背景写死，与 Theme 不同步

- **位置**：`GuiOpenSettings` `Background = COLOR_LIGHT_GRAY`  
- **方向**：纳入 `Theme*` 或独立键（体验一致性，非闪屏主因）。

---

## 3. Low / 结构

| ID | 说明 | 方向 |
|----|------|------|
| L1 | `Gui.c` ~2100 行：窗管 + 合成 + 光标 + 焦点 | 拆 `GuiCompositor` / `GuiCursor` |
| L2 | `GuiOnMouse` 与 `GuiPollMouse` 双份按钮边沿 | 统一到一处 |
| L3 | `WinCopy` 手写字节拷贝 | 结构体赋值 |
| L4 | `FillRectOccluded` O(像素×窗) | 矩形裁剪 / 只 blit 备份 |
| L5 | `VideoCopyRect` 逐像素 | 按行拷贝（注意 pitch） |
| L6 | `DesktopDraw`（Raw）若在已有窗时被误调会盖窗 | 注释/断言/改名 |
| L7 | Console `GuiBackupSyncRect` 包围盒在滚屏后可能不全 | 滚屏后 sync 整客户区 |
| L8 | 双击时限绑裸 TSC，与「~0.5s」注释随 CPU 变 | 用校准毫秒时钟 |
| L9 | Arm64/RiscV `HalVideoClearClip` 空实现 | 跨架构对齐或 `#ifdef` 文档说明 |

---

## 4. 场景 × 机制对照

| 场景 | 主要机制 | 锚点 |
|------|----------|------|
| 拖动 | 脏区离屏有帮助；cli 过长；analytic under-drag；事后 `DesktopDrawRect`；无 vsync | `CompositeDrag*` / `MoveWindowTo` |
| `GuiRedraw` | 填色→Raw 图标→窗，一次 cli，相对干净；仍单缓冲可撕 | `GuiRedraw` |
| Theme | 多遍清屏/写穿/盖回 | `ThemeApply` |
| 光标 | save-under 正确；拖动中隐藏；开窗未统一 erase | `Cursor*` / `GuiOpen*` |
| 关窗 | 填桌面 + 只画 chrome | `CloseWindow` |

---

## 5. 非 GUI（顺带，未深挖）

教学内核整体可后续单独审；与桌面阶段弱相关、可记一笔：

- **SMP / 调度**：AP idle 与 GUI 同核抢占策略是否文档化。  
- **FAT / Theme 写盘**：`ThemeSave` 手写拼串；失败路径与串口日志是否足够。  
- **系统调用 / 用户态**：与 GUI 无直接关系；保持路线图边界即可。

---

## 6. 一句话

闪屏主因不是「少画几个矩形」，而是：

1. 起始洞合成丢掉下层真实像素（analytic under-drag）；  
2. 关窗 / 主题未走同一套合成器；  
3. 长 `cli` 与 USB 鼠标打架；  
4. 单缓冲直出。

按 §0.3 顺序修，可在不大改教学架构下明显改善拖动与换主题；完整消撕裂需要 backbuffer 级 Present。
