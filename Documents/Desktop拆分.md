# 任务：拆分 Desktop.c（PR-S-desktop-split-1 / -2 / -3）

> **规格同 [`大文件拆分.md`](大文件拆分.md)**：只搬家、不改逻辑；`Desktop.h` 不动；每刀 build + `smoke-boot`。  
> **本文件 = 轨 D · Desktop 细则**；总盘点见 [`大文件拆分3.md`](大文件拆分3.md)。  
> **★ 柱完成**：三刀均已落地（split-3 本地待 TG）。  
> **统计时点**：2026-09-16；split-1 ✅ TG `80fcf40`。

### ★ 拆分进度

| 状态 | PR | 内容 | 说明 |
| --- | --- | --- | --- |
| ✅ TG `80fcf40` | **PR-S-desktop-split-1** | `Include/DesktopPriv.h` + `DesktopWallpaper.c` | 壁纸缓存 / BgAt / FillRect；`gDeskSelected` |
| ✅ TG `f5f6ab8` | **PR-S-desktop-split-2** | `DesktopPaint.c` | FillRectFree / Draw* |
| ✅ 本地 | **PR-S-desktop-split-3** | `DesktopMenu.c` + `DesktopIcons.c` | 菜单 + 图标/布局；`MenuCopyStr` 导出 |

**行数（约）**：`Desktop.c` ~700；Wallpaper ~133；Paint ~280；Menu ~278；Icons ~436。  
**验收**：split-3 `./build.sh` OK；串口 desktop ready + ToyOS ready PASS（2026-09-16）。

### 刀序（相对旧总表修订）

1. Wallpaper（最独立）→ 2. Paint → 3. Menu + Icons。

### 已知符号修正

| 项 | 说明 |
| --- | --- |
| `gSelected` | 与 FilesUi 冲突 → **`gDeskSelected`**（仅 Desktop 族） |
| `IconDesktop48.inc` | 不存在；忽略 |

---

其余函数清单与验收项见会话分析 / 用户规格；完成后回写 [`大文件拆分3.md`](大文件拆分3.md) 轨 D。
