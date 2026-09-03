# ToyKernel 代码审阅（侧重 GUI / 闪屏）

**日期**：2026-09-03  
**基线**：`main` @ `72ebc1e`（PR-D6 + 拖窗图标恢复）  
**范围**：以 `Gui.c` / `Desktop.c` / `Theme.c` / `Console.c` / `SettingsUi.c` / `Video.c` 为主；顺带列出可改进点。  
**结论**：拖动路径已有「脏区离屏一次写出」（PR-G5）等正确方向，但仍是 **单缓冲直写 GOP** + **关中断临界区过长** + **合成语义不完整** 的组合；重叠窗拖动 / 关窗 / 换主题时仍易闪屏、撕裂或内容残缺。

---

## 0. 闪屏问题总览（优先读）

### 0.1 根因分层

| 层 | 问题 | 用户体感 |
|----|------|----------|
| **呈现** | 直接写 scanout FB，无 backbuffer / vsync | 多段更新被扫描线看到 → 撕裂、灰闪 |
| **同步** | 用长 `cli`（`HalIrqDisable`）冒充「帧原子性」 | USB 鼠标 IRQ 饿死 → 卡顿、跳变，松手后「闪一下追上」 |
| **合成语义** | 起始 footprint 的 under-drag 用 **解析像素**（空壳窗），不读下层备份 | 拖开重叠窗后下层文字被抹掉；再局部补画 → 二次闪 |
| **更新策略** | Theme / 关窗走「破坏性重绘」而非同一套 Compose | 中间帧对用户可见（清屏→写穿→盖回） |
| **光标/备份** | 开窗等路径未统一擦光标再备份 | 十字残影烙进窗备份 |

已做好、应保留：整窗备份保留客户区文字；拖动脏区离屏一次 `WriteRect`；`GuiRedraw` 用 Raw 图标避免 cli 下逐像素避让。

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

### H1. under-drag 用解析像素盖掉下层真实内容

- **位置**：`Gui.c` `AnalyticWindowPixel` / `TopmostBelowDragPixel`（约 728–773）；`CaptureDragRestoreData`；`CompositeDragPixel` 优先 under-drag  
- **问题**：拖走后原位置应露出「被拖窗下方」的场景。当前用标题栏色/客户区底色/边框 **解析生成**，不读下层 `gWinBackup`。下层 Shell 文字、Settings 菜单在起始 footprint 重叠区会被抹成空窗。全屏 snap 有正确像素，但在起始 footprint 被 under-drag 抢先。  
- **方向**：`TopmostBelowDragPixel` 优先 `SampleWindowBackupPixel`；仅备份无效时退回 analytic。或抓 under-drag 时按 z 序从各窗备份合成。

### H2. 关窗只填桌面 + 重画 chrome，不恢复客户区

- **位置**：`Gui.c` `CloseWindow`（约 318–364）  
- **问题**：`UiFillRectangle` 桌面色 → `DesktopDrawRect` → 对剩余窗只 `DrawWindowChromeAt`。被关窗盖住的下层 **客户区文字不会从备份恢复**。  
- **方向**：对 footprint 相交的窗 `PaintWindowFromBackup`（或整窗贴回 + chrome）；无备份则 `DrawWindowAt` + Shell/Settings 内容回调。

### H3. 裸 `HalIrqDisable` 临界区过长 → USB 鼠标饿死

- **位置**：`MoveWindowTo`、`StartDragBackups` / `CaptureDragRestoreData`（含全屏 `ReadRect` + 逐像素 under-drag）、`CloseWindow`、`CursorMove` / `RedrawWindowChrome` 等；对比可嵌套的 `GfxIrqEnter/Leave`（约 107–119）  
- **问题**：`cli` 期间 XHCI 鼠标 IRQ 不递送。脏区可达旧∪新窗，再叠加 `DesktopDrawRect` 逐像素避让（`Desktop.c` `FillRectFree`），cli 可持续数 ms～数十 ms。注释已承认避让在关中断下会拖死 USB（`Desktop.c` 约 162），但拖动脏区仍在 cli 下调用 `DesktopDrawRect`。  
- **方向**：  
  1. 统一 `GfxIrqEnter/Leave`，禁止散落 `HalIrqEnable` 破坏嵌套。  
  2. 只保护光标与单次 `WriteRect`；合成写离屏缓冲时开中断。  
  3. 全屏 snap / under-drag 构建移出 cli 或分片。

### H4. ThemeApply 多遍全屏/多窗重绘，闪屏几乎必然

- **位置**：`Theme.c` `ThemeApply`（约 58–69）；`GuiApplyThemeColors` → `GuiRedraw`；`ConsoleRepaintShellWindows`；`SettingsUiRefresh`  
- **问题**：整屏重画（客户区清空）→ Shell 文字重画（**可穿透上层窗**，Theme 注释已写明）→ Settings 整窗盖回 → 再 `GuiBackupAllWindows`。用户可见灰闪、文字闪、写穿再覆盖。  
- **方向**：单次场景合成；Shell 只画进本窗备份再 blit；勿先清空再拼。

### H5. 无双缓冲 / 无 vsync

- **位置**：`Video.c` `VideoDrawPixel` / `FillRect` / `WriteRect`；GUI 最终落点  
- **问题**：关窗填色后再画 chrome、Theme 多遍、拖动行缓冲回退逐行写等中间状态会被扫描线看到。`gDragDirty` 一次写出只缓解拖动，不消除撕裂。  
- **方向**：与屏同尺寸（或脏矩形）backbuffer；Present 一次 blit；条件允许等 VBlank。

---

## 2. Medium

### M1. 合成后再 `DesktopDrawRect`：二次写屏 + 拉长 cli

- **位置**：`Gui.c` `CompositeDragDirtyRegion` 末尾；`Desktop.c` occluded 路径  
- **问题**：为补 `DesktopSamplePixel` 不含标签，提交后再避让重画图标。第二次写 FB、cli 更长。  
- **方向**：合成阶段完整采样 icon+label（或 icon atlas）；提交后不再 `DesktopDrawRect`，或仅在 sti 后补标签子矩形。

### M2. `DesktopSamplePixel` 与真实绘制不一致

- **位置**：`Desktop.c` 采样 vs `DrawOneIconRaw` / `Occluded`  
- **问题**：只还原 48×48 色块；标签依赖事后重画；拖过图标时文字易闪。  
- **方向**：采样含字形，或与绘制共用几何/预渲染。

### M3. 开窗不隐藏光标、不走统一 IRQ 锁

- **位置**：`GuiOpenShell` / `GuiOpenSettings`  
- **问题**：`DrawWindowAt` / `BackupWindowAt` 时十字可烙进窗与备份；随后 chrome 刷新可能残影。  
- **方向**：与 `GuiPaintWindow` 一样：`GfxIrqEnter` + `CursorRestore` … `CursorPaint`。

### M4. `DrawWindowAt` 标题无逐像素遮挡

- **位置**：完整 `DrawWindowAt` vs chrome 路径有 occlusion  
- **问题**：Theme / 开窗 / `PaintAllWindowsDraw` 回退时标题可写穿上层客户区。  
- **方向**：occlusion，或只 blit 本窗备份。

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

### M8. 裸 `HalIrqEnable` 与 `GfxIrq*` 嵌套冲突

- **位置**：`CloseWindow` / `MoveWindowTo` 等 vs `GuiFrameBufferBegin`  
- **问题**：嵌套时末尾 `Enable` 可在 `gGfxLockDepth>0` 时误开中断；亦不保存进入前 IF。  
- **方向**：全部改 `GfxIrqEnter/Leave`。

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
