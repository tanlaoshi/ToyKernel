# ToyOS GUI 美化规划

> **本文是 GUI 美化方向的总纲。** 后续所有 GUI 美化 PR 都引用本文。  
> **排期**：全仓 **P1**（[`路线图.md`](路线图.md#pr-gui)）；**P0 是命名 R 柱**。本柱不插队。`PR-GUI-doc` 已入库。
>
> 相关文档：
> - [`技术手册.md`](技术手册.md#tm-iv-gui) — 第一个 GUI 程序 / 用户态控件 ABI
> - [`技术手册.md`](技术手册.md#tm-iv-fonts) — 点阵字体与 TOYF
> - [`Done/应用开发生态规划.md`](Done/应用开发生态规划.md) — 应用开发方向总纲
> - [`路线图.md`](路线图.md) — 总体排期

---

## 一、目标

让 ToyOS 的 GUI 从「能用」提升到「好看」，同时保持：

- **纯 C**（不引入 C++）
- **无依赖**（静态库 + 无动态链接 + 不引入 cairo/skia/SDL）
- **教学优先**（代码可读、每行能懂）
- **性能不回归**（真机 NUC 上鼠标流畅）

**核心原则**：

- 先视觉基础（配色/间距/边框），再视觉进阶（阴影/渐变），最后交互（淡入淡出 / 按钮态）
- 每步可独立编译、可独立验证
- 不破坏现有功能（Shell / Files / Settings 不回归）
- **不规划窗口 chrome 圆角**（`PR-GUI-l2-round` 已删）；控件圆角 `Ui*RoundRectangle` 已有，按钮按需用即可

---

## 二、现状盘点

路径以 **2026-09-16 仓库**为准（大文件拆分后）。

### 2.1 ToyOS GUI 的组成

| 模块 | 文件 | 职责 | 约行 |
|------|------|------|------|
| 窗口管理 | `Common/Services/GuiWm.c` | 窗口表、Raise、命中测试 | 179 |
| 合成 | `Common/Services/GuiCompose.c` | 桌面+窗口→后缓冲→Present | 228（已拆；见 [`Done/GuiCompose拆分.md`](Done/GuiCompose拆分.md)） |
| 拖动 | `Common/Services/GuiDrag.c` | 拖动备份、脏区、合成 | 678 |
| 光标 | `Common/Services/GuiCursor.c` | save-under、擦除、绘制 | 237 |
| 焦点 | `Common/Services/GuiFocus.c` | 焦点切换、输入路由 | 313 |
| 用户窗 | `Common/Services/GuiUser.c` | 用户态窗口协议 | 504 |
| 桌面 | `Common/Services/Desktop.c` | 图标、任务栏、开始菜单、壁纸 | 699 |
| 控件 | `Common/Library/UI.c` | 几何 + 按钮/列表行/滚动条 | 455 |
| 主题 | `Common/Services/Theme.c` | 桌面/Shell 底色、字体、分辨率、缩放 | 758 |
| 字体 | `Fonts/FontRegistry.c` | 点阵注册与 `Font*` 绘制入口 | 591 |
| 帧缓冲 | `HAL/X64/Drivers/Video.c` | GOP、后缓冲、脏区（G9） | 800 |
| Present | `HAL/X64/Drivers/VideoPresent.c` | 脏区 blit 到 scanout | — |
| 像素矩形 | `HAL/X64/HalVideo.c` | `HalVideoWriteRect` 等 | — |
| BMP | `Common/Library/Bmp.c` | BI_RGB 24/32 bpp → RGB888（G13） | 124 |

控件 API：[`Include/UI.h`](../Include/UI.h)。主题 API：[`Include/Theme.h`](../Include/Theme.h)。

### 2.2 视觉现状

| 维度 | 现状 | 评价 |
|------|------|------|
| **颜色** | 大量 `COLOR_BLUE` / `COLOR_LIGHT_GRAY` / `COLOR_DARK_GRAY`；Theme 只管桌面/Shell 底色等 | 控件/窗框未统一走 Theme，层次弱 |
| **字体** | 点阵：默认倾向 Terminus **10×18** / Sun 8×16；16×32 为旧默认；CJK 16×16；运行时 TOYF | 边缘硬，无抗锯齿 |
| **窗口边框** | 窗框仍是矩形 1px 级描边 | 窗口扁平 |
| **控件圆角** | **已有**：`Ui*RoundRectangle`；`UiDrawButton` 圆角 5 | 按钮可用；**不接窗口 chrome** |
| **阴影** | ✅ `DrawWindowShadowAt`（L2-shadow） | 有右/下 drop shadow |
| **图标** | 桌面 48×48 BMP（G13）或纯色块 | 能用，风格不统一 |
| **动画** | ✅ 窗口淡入淡出（L3-fade，`GuiFade.c`） | 开关窗有中间帧；按钮态待做 |
| **光标** | 简单箭头 | 可接受 |
| **文字排版** | 左对齐 + 固定行高 | 标题/正文/提示未分层 |
| **任务栏 / 开始菜单** | 纯色矩形 + 按钮/列表 | 简陋 |

### 2.3 底层能力盘点

GUI 底层已经具备做美化的大部分条件：

| 能力 | 现状 | 可用于 |
|------|------|--------|
| 后缓冲 | ✅ G9（`Video.c`） | alpha 混合、局部重绘 |
| 脏区 Present | ✅ G9 + `VideoPresent.c` | 局部动画 |
| 窗口备份 | ✅ 拖动/合成路径 | 阴影底 |
| 字体注册 | ✅ T3（`FontRegistry.c` + TOYF） | 加新字体/灰度字 |
| 主题系统 | ✅ `Theme.c`（底色/字体/mode/缩放） | **扩展**统一配色，勿另起炉灶 |
| BMP | ✅ `Bmp.c`（无 alpha 通道） | 图标；抗锯齿图标需扩格式 |
| 像素 blit | ✅ `HalVideoWriteRect` | 画任意图形 |
| 圆角几何 | ✅ `UI.c` 已实现 | 控件/按钮用；**不规划窗框圆角** |

**缺的是「把已有能力接到窗口/桌面主路径」**，不是从零写光栅器。

---

## 三、「丑」的根因

### 3.1 设计取舍

| 选择 | 代价 |
|------|------|
| 点阵 1bpp 字体 | 无抗锯齿 |
| 控件/窗框硬编码 `COLOR_*` | Theme 管不全，无层次 |
| 窗口几何仍是直角矩形 | 刻意保持；控件圆角另用 |
| 曾无 alpha | 现已有混合；半透明菜单已冒烟 |
| 曾无动画循环 | 淡入淡出已有；按钮态待做 |

教学优先：学生能看懂每一行；视觉不是当时的交付目标。

### 3.2 不是「不能做」，是「没接到主路径」

已有控件圆角/Theme/后缓冲/BMP/alpha/阴影/渐变/淡入淡出。还缺：

- 按钮悬停/按下（L3 余下）
- 4bpp/8bpp 字体（L2 最难，可后置）

---

## 四、改进方向（三层）

### 4.1 L1：视觉基础（2–3 天）

| 改进 | 说明 | 工作量 |
|------|------|--------|
| **统一配色** | 控件/窗框/任务栏改走 Theme（扩展，不拆现有 getter） | 1 天 |
| **三态边框** | 焦点/非焦点/悬停三种边框色 | 0.5 天 |
| **统一间距** | 客户区内边距约定（如 8px） | 0.5 天 |
| **文字层次** | 标题 / 正文 / 提示（可用已有多字号，不必新字体） | 0.5 天 |
| **图标统一** | 桌面图标 48×48 风格对齐 | 0.5 天 |

**收益**：从「能看」到「顺眼」。不依赖 alpha。

### 4.2 L2：视觉进阶（5–10 天）

| 改进 | 说明 | 工作量 | 技术要点 |
|------|------|--------|----------|
| **alpha 混合** | 后缓冲逐像素混合 | 1 天 | 后续阴影/渐变/动画的底座 |
| **窗口阴影** | 窗外一圈半透明黑 | 2 天 | alpha + 窗口几何 |
| **标题栏渐变** | 逐行插值 | 1 天 | 可先不依赖 alpha |
| **图标抗锯齿** | BMP 扩 alpha / 32bpp 真 alpha | 1 天 | 现 `Bmp.c` 仅 BI_RGB |
| **抗锯齿字体** | 4bpp/8bpp + Font 渲染 | 3–5 天 | TOYF 扩展；**可后置** |
| **多套主题** | Theme 表驱动多套色板 | 1 天 | 只扩展 `Theme.h` |

**收益**：从「顺眼」到「好看」。

### 4.3 L3：交互体验（5–10 天）

| 改进 | 说明 | 工作量 |
|------|------|--------|
| **窗口淡入淡出** | 开关窗中间帧（✅ 已入 **PR-GUI-l3**） | 2 天 |
| **按钮悬停/按下** | 变色 + 凹陷（← **PR-GUI-l3** 余下） | 1–1.5 天 |
| **菜单展开** | 开始菜单下拉 | 2 天 |
| **光标拖尾** | 可选 | 1 天 |
| **提示气泡** | 悬停 hint | 1 天 |

**收益**：从「好看」到「舒服」。真机 FPS 不够则做开关或仅 QEMU。淡入 + 按钮态合为 **PR-GUI-l3** 一刀。

---

## 五、技术要点

### 5.1 alpha 混合（L2 底座）

```c
UINT32 VideoBlendRgb(UINT32 Dst, UINT32 Src, UINT8 Alpha) {
    UINT32 R = (((Src >> 16) & 0xFFu) * Alpha + ((Dst >> 16) & 0xFFu) * (255u - Alpha)) / 255u;
    UINT32 G = (((Src >> 8) & 0xFFu) * Alpha + ((Dst >> 8) & 0xFFu) * (255u - Alpha)) / 255u;
    UINT32 B = ((Src & 0xFFu) * Alpha + (Dst & 0xFFu) * (255u - Alpha)) / 255u;
    return (R << 16) | (G << 8) | B;
}
```

写后缓冲，再走现有脏区 Present。门面：`HalVideoBlend*` / `UiFillRectangleAlpha` / `UiBlendRgb`。用途：阴影、半透明、灰度字、淡入淡出。

### 5.2 阴影（L2）

窗外 N 像素（`ThemeWindowShadowSize`，默认 6）右/下 drop shadow：alpha 贴边约 `ThemeWindowShadowMaxAlpha`（128）收到 0。`DrawWindowShadowAt` + `HalVideoBlendFillRect`；命中仍矩形（阴影不进 `PointInWindow`）。拖动：`ExpandRectByWindowShadow` 扩 footprint，`RedrawDragFrame` / `MoveWindowTo` 同步清残影。

### 5.3 标题栏渐变（L2）

顶色仍走三态 `ThemeWindowTitleFocus/Idle/Hover`；底色 `ThemeWindowTitleGradientBottom(Top)`（向黑约 40%）。直角标题栏按行 `TitleBarColorAtRow` 填充。`AnalyticWindowPixel` 同步按行取色。不改命中矩形；暂不进 THEME.CFG。

### 5.4 交互（L3）— **PR-GUI-l3**（淡入淡出 + 按钮态）

**淡入淡出**（✅ TG `a9d4875`）：`ThemeWindowFadeSteps()`（默认 6；`THEME.CFG` 键 `fade=`，`0`=关）。开/关窗：捕获无本窗桌面层 → 与窗备份逐帧 `HalVideoBlendRgb` → Present。实现：`GuiFade.c` / `GuiAnimateWindowFade`。阴影不参与中间帧（首/末随合成）。目标约 ≥30 FPS 手感；NUC 卡则 `fade=0`。

**按钮悬停/按下**（Settings 已接）：`UiDrawButtonEx` + `SettingsUiOnPointer`（悬停提亮、按下凹陷）；控件圆角沿用 `Ui*RoundRectangle`。**不**再开窗口 chrome 圆角刀。

### 5.5 抗锯齿字体（L2 最难，可后置）

现状：1bpp 点阵（`Font*` / TOYF）。灰度字需要新格式 + 加载 + alpha 绘制。工作量最大，排 P3。

---

## 六、技术栈决策

### 6.1 保持纯 C

不引入 C++ / STL / g++。L1–L3 均可 C 实现。

### 6.2 不引入图形库

不引入 cairo / skia / SDL（glib/freetype/pthread 与「无依赖」冲突）。

### 6.3 使用现有能力

后缓冲、脏区、`UI.c` 圆角、Theme、BMP、`Font*`。优先接到主路径，不新开一套绘制栈。

---

## 七、PR 计划

### 7.1 PR 列表

| 序 | PR | 交付物 | 工作量 | 优先级 |
|----|----|--------|--------|--------|
| 0 | **PR-GUI-doc**（本文） | `Documents/GUI美化规划.md` | 0.5 天 | 已入库 |
| 1 | **PR-GUI-l1** | L1：Theme 扩展 + 间距 + 三态边框 | 2–3 天 | ✅ TG `7417d76` |
| 2 | **PR-GUI-alpha** | 后缓冲 alpha 混合 | 1 天 | ✅ TG `cc7965e` |
| 3 | **PR-GUI-l2-shadow** | 窗口阴影（含拖动） | 2 天 | ✅ TG `f1772d7` |
| 4 | **PR-GUI-l2-gradient** | 标题栏渐变 | 1 天 | ✅ TG `82978dd` |
| 5 | **PR-GUI-l3** | L3：淡入淡出 + 按钮悬停/按下 | 3–3.5 天 | fade ✅ `a9d4875`；button ← **JX** |
| 6 | **PR-GUI-l2-font** | 抗锯齿字体（可选） | 3–5 天 | P3 |

> ~~PR-GUI-l2-round~~（窗口 chrome 圆角）**已取消**，不再排期；控件 `Ui*RoundRectangle` 仍可用。

### 7.2 推荐顺序

```
PR-GUI-doc（本文）✅
  → PR-GUI-l1（视觉基础）
  → PR-GUI-alpha
  → PR-GUI-l2-shadow
  → PR-GUI-l2-gradient   ✅ `82978dd`
  → PR-GUI-l3            ← 当前（fade ✅ `a9d4875`；button 余下）
  → PR-GUI-l2-font（可选）
```

先 L1，再 alpha + 阴影 + 渐变；**PR-GUI-l3** 合并淡入淡出与按钮态（fade 已交付，下一刀做 button）；灰度字后置。**不**再开窗口圆角刀。

**进 JX**：须 R 柱 0～7 空，或明文改路线图文首 ★。表内「P0/P1」是**柱内**性价比，不是全仓优先级。

### 7.3 每个 PR 的交付物

| 交付物 | 说明 |
|--------|------|
| **文档** | 本文件对应小节 + 路线图归档（TG 时） |
| **代码** | 真实 `.c` 改动 |
| **示例** | QEMU 可见：开窗 / 拖动 / Files 或 Settings 不回归 |
| **验收** | `./build.sh` + QEMU；有条件真机 NUC |

---

## 八、优先级与性价比

| 优先级 | 改进 | 工作量 | 视觉收益 |
|--------|------|--------|----------|
| **P0** | L1 统一配色 | 1 天 | 高 |
| **P0** | L1 三态边框 | 0.5 天 | 中高 |
| **P0** | alpha 混合 | 1 天 | 底座 |
| **P0** | L2 窗口阴影 | 2 天 | 最高 |
| **P1** | L2 标题栏渐变 | 1 天 | 中 |
| **P1** | L3 淡入淡出 + 按钮态（**PR-GUI-l3**） | 3–3.5 天 | 高 |
| **P2** | 图标 alpha | 1 天 | 中 |
| **P3** | 抗锯齿字体 | 3–5 天 | 最高但贵 |
| **P3** | 其它动画 | 3–4 天 | 中低 |
| — | ~~L2 窗口圆角~~ | — | **已取消** |

### 性能约束（真机 NUC）

| 场景 | 目标 |
|------|------|
| 鼠标移动 / 窗口拖动 | 流畅（等效 >60 FPS 手感） |
| 开关窗动画 | ≥ 30 FPS，可关 |
| 阴影 | 不拖垮拖动 |

卡顿则提供低配开关，或降级「仅 QEMU」。

---

## 九、硬约束

1. **不改 syscall 号**；Theme / UI **只扩展不破坏**
2. **不改现有功能语义**：Shell / Files / Settings / 用户窗 Poll 不回归
3. **保持纯 C**；不引入图形库
4. **改动落点**：`Common/Services/Gui*.c`、`Desktop.c`、`Theme.c`、`Common/Library/UI.c`、`Bmp.c`、`Fonts/`、`HAL/X64/Drivers/Video*.c`、`HalVideo.c`（按需）。**不要**只改 `Video.c` 却漏掉 Library
5. **每 PR 独立可验证**：编译 + QEMU；NUC 有条件
6. **性能不回归**：鼠标/拖动流畅
7. **`_template/` 与驱动柱无关**；本柱不改 Driver* API

---

## 十、与其它方向的关系

| 方向 | 关系 |
|------|------|
| **应用开发生态** | 内核窗/控件更好看，用户态 `libToyUi` 间接受益；**不插队 P0 命名 R 柱** |
| **驱动框架标准化** | 不涉及 |
| **大文件拆分** | `GuiCompose.c` 已拆（勿再按 984 行估）；`Desktop.c` ~699、`GuiDrag.c` ~678 仍偏大，美化 PR **禁止顺手大搬家** |
| **C++** | 本柱纯 C |

应用开发者要好看的控件：L1/L2 做在内核 `UI.c` / 窗框上，比先改 `libToyUi` 杠杆更高。`libToyUi` 客户区控件另计，须保持 ABI 小版本规则。

---

## 十一、相关文档

| 文档 | 作用 |
|------|------|
| [`技术手册.md`](技术手册.md#tm-iv-gui) | 用户态 GUI 入门 |
| [`技术手册.md`](技术手册.md#tm-iv-fonts) | 字体 |
| [`Done/应用开发生态规划.md`](Done/应用开发生态规划.md) | 应用生态总纲 |
| [`路线图.md`](路线图.md) | 排期 / JX |
| [`Done/GuiCompose拆分.md`](Done/GuiCompose拆分.md) | 合成拆分史 |
| **本文** | GUI 美化总纲 |

---

## 十二、修订记录

| 日期 | 说明 |
|------|------|
| 2026-09-16 | 初版。按仓库校正路径（`UI.c`/`Bmp.c` 在 Library；无 `Font.c`；Compose 已拆）；圆角 API 已存在；Theme 已部分覆盖。**PR-GUI-doc** |
| 2026-09-16 | 排期对齐路线图：本柱全仓 **P1**；不插队 P0 命名 R 柱 |
| 2026-09-17 | **PR-GUI-l1**：Theme 窗框/任务栏/控件 getter；三态边框+悬停；`ThemeClientPadding`=8 |
| 2026-09-17 | 取消窗口 chrome 圆角（删 §5.2 / l2-round）；**PR-GUI-l3** 合并 fade+button；现状表补阴影/淡入；控件圆角仅作按钮能力说明 |
