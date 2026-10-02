# ToyOS UI 颜值与布局（活文档）

> **状态**：规格活文档（2026-10-03）；排期认 [`路线图.md`](../路线图.md#pr-ui-look)。  
> **口号**：颜值即是战斗力。本柱只改**看起来像不像正经桌面**，不改 syscall / 不接 TTF。  
> **对照（已收官，勿复开）**：[`已完/GUI美化规划.md`](../已完/GUI美化规划.md)（L1–L3 阴影/渐变/淡入）；[`已完/科技感主题规划.md`](../已完/科技感主题规划.md)（tech 色板）。  
> **政策**：路线图 **D.9** — 不用矢量字当默认、不承诺完整 Unicode。汉字仍走点阵；本柱把**课用中文 UI 补全、画清楚**。

---

## 一、问题（人话）

课堂现在能开窗、能换主题、能 `lang zh`，但三件事把「玩具」钉死在脸上：

| # | 现象 | 根因（仓库实况） |
| - | ---- | ---------------- |
| 1 | **配色不现代**；上次 **tech** 不好看，半透明/渐变/淡入还不如默认 | `theme=default\|tech\|tech-grad`；chrome 色大量仍像 Win9x 蓝灰；tech = 深青黑 + 霓虹青（`ThemeTech.c`）；效果级别 `minimal/low/medium/high` 默认 low，但标题渐变/阴影叠在默认主题上仍「特效感」 |
| 2 | **程序布局 low**：列表顶满、详情栏像调试打印、空文件夹一句英文、对话框居中灰块 | Settings / Files / Store 三分栏几何各写一套 `+8`/`+10`；大量 `HalVideoDrawStringAt(..., "Detail", …)` **不走** `LocStr`；无统一行高/侧栏宽/底栏 |
| 3 | **汉字丑且缺**：切中文后很多字直接不画（像抽了） | `cjk16.c` 仅 **137** 码点、16×16 1bpp 再拉伸到 Terminus 行高；`VideoDrawCodepoint` 缺字形 **直接 return**（吞字）。`gZhFallback[]` 里大量汉字（商店/设备/贪吃蛇/缩放/经典/科技/保存/重启…）**不在**这 137 里 |

**本柱不做**：Wayland、TTF 默认、完整 Unicode、重做 WM、新特效引擎、再打磨 tech 霓虹。

---

## 二、硬约束

| # | 钉死 |
| - | ---- |
| 1 | 纯 C；不引入 cairo / skia / FreeType / 矢量默认 |
| 2 | Theme / UI **只扩展签名**；`COLOR_*` 宏不当唯一色源（新默认走 Theme getter） |
| 3 | 新 `.c` ≤300；点阵表 / 生成脚本算例外（原则 7） |
| 4 | 三架构可编；`./build.sh` + `smoke-boot`；GUI 刀 **要手测**（QEMU 1280×720：桌面 / Settings / Files / Store / `lang zh`） |
| 5 | **tech 主题保留可切换，本柱不把它当默认、不追加 tech 特效** |
| 6 | 效果默认维持 **low**；禁止把 medium/high（更重阴影/淡入）设成出厂默认 |
| 7 | 汉字覆盖以 **内建 zh 文案 ∪ 本柱新文案** 为验收集，**不是** GB2312 整表 |

---

## 三、方向 1 · 配色（现代浅色，替换「默认不好看」）

### 3.1 策略

- **出厂默认改为一套「现代浅色」**（新 id：`theme=modern`，Settings 名「现代」；**装完即是这套**）。  
- 旧 `default` 改名单为「经典」（Win9x 蓝灰），仍可切换，避免有人怀旧。  
- `tech` / `tech-grad` **冻结**：能选、不修色、不进推荐。  
- **不**用新渐变/霓虹/扫描线证明「现代」。现代 = 干净底、克制强调色、文字对比够、控件面和窗框分层清楚。

### 3.2 色板（`0x00RRGGBB` · 钉死，实现按表抄）

设计：浅灰工作台 + 白客户区 + 石板标题 + 一条蓝灰强调（饱和度低于 tech 青）。

| 用途 | 色值 | 说明 |
| ---- | ---- | ---- |
| 桌面背景 | `0x00E6E8EC` | 冷浅灰，不是 40% 中灰 |
| 客户区 / Settings 底 | `0x00F7F8FA` | 略亮于桌面 |
| Shell 客户区 | `0x00F4F1EA` | 略暖，和桌面分开 |
| 标题栏焦点 | `0x002C3038` | 深石板，**不要** `COLOR_BLUE` |
| 标题栏空闲 | `0x005C616A` | |
| 标题文字 | `0x00F4F5F7` | 深底浅字 |
| 窗框焦点 | `0x003A4050` | 1px，无发光 |
| 窗框空闲 | `0x00C5CAD3` | |
| 关闭钮 | `0x00C0423A` | 克制红，非品红霓虹 |
| 任务栏 | `0x00DDE1E7` | 浅，**不**半透明到看不清 |
| 任务栏按钮 / 激活 | `0x00F7F8FA` / `0x002C3038` | 激活深底 |
| 任务栏文字 | `0x001C1E22` | |
| 控件面 / 边框 | `0x00FFFFFF` / `0x00C5CAD3` | |
| 强调（选中、主按钮） | `0x003D5A80` | 石板蓝，**禁止** `00D0FF` |
| 强调上文字 | `0x00F7F8FA` | |
| 正文 / 弱文 / 分隔 | `0x001C1E22` / `0x005C616A` / `0x00D0D5DD` | |
| 列表选中底 | `0x00D7E2EE` | 浅强调底 + 深字 |
| 对话框面 | `0x00FFFFFF` | |

**壁纸**：modern 默认 `wallpaper=1`（有 `WALL.BMP` 就铺；没有回退纯色）。不强制关壁纸（那是 tech 的做法）。

**效果**：modern **不**改 `fade=` 默认；标题渐变在 modern 下 **顶底同色**（视觉上关掉渐变）。阴影保留 low 档尺寸，不加大。

### 3.3 验收

- Settings → 主题 → 现代：桌面/窗/任务栏一次变齐；DB `theme=modern`。  
- 切回经典 / tech：行为与现网一致（回归）。  
- QEMU 截图：无霓虹描边、无大块纯蓝标题栏。

---

## 四、方向 2 · 布局（同一套格子，三个程序共用）

### 4.1 低的具体表现（按程序）

| 程序 | 现在 | 目标 |
| ---- | ---- | ---- |
| **Settings** | 左栏/中列表/右「Detail」；详情里混英文 hint、色块挤在字下；分类像调试菜单 | 左导航（固定宽）+ 中选项 + 右说明；页标题一行；选项行高统一；色块与标签横排 |
| **Files** | 顶一行路径+英文 Esc；空目录两行英文快捷键；覆盖层灰框 | 顶栏：路径 + 视图；空态居中短中文；对话框有标题/正文/按钮行 |
| **Store** | 右栏 `Detail` / `id:` / `type:` 像 log；底栏按钮挤 | 与 Settings 同侧栏宽、同行高；详情键值对齐；底栏右对齐主操作 |
| **Devices** | 摘要/详情纯文本堆 | 沿用三分栏：左类、中列表、右只读说明 |
| **桌面** | 图标网格能用；开始菜单矩形列表 | 图标间距/标签边距表驱动；开始菜单分组（应用 / 电源）+ 行高 |

### 4.2 布局令牌（实现放 Theme 或 `UiLayout.h`，禁止各 UI 再魔法数）

目标分辨率 **1280×720**（其它分辨率按现 `scale` 走，令牌先按 100%）。

| 令牌 | 值 | 用途 |
| ---- | -- | ---- |
| `Pad` | 12 | 客户区内边 |
| `Gap` | 8 | 控件间隙 |
| `SideW` | 168 | 左导航宽 |
| `DetailMinW` | 220 | 右详情最小宽 |
| `RowH` | `max(FontAdvanceY()+8, 28)` | 列表/导航行 |
| `BarH` | 40 | 窗内顶栏/底栏（与标题栏 40 对齐） |
| `BtnW` / `BtnH` | 96 / 32 | 主按钮 |
| `IconGap` | 32 | 桌面图标间距（现 28 略挤） |

三分栏公式（客户区宽 `W`）：

```text
Side  = SideW
List  = W - SideW - max(DetailMinW, W/3) - 2*分隔
Detail= 其余
分隔  = 1px Theme 分隔色
```

空状态：详情区垂直居中 **一行标题 + 一行弱提示**，不要贴顶堆英文。

### 4.3 状态必须画出来（实现时勿只做「有内容」）

| 面 | 状态 |
| -- | ---- |
| 列表 | 默认 / 选中 / 悬停 / 空 |
| 按钮 | 默认 / 悬停 / 按下 / 禁用（Store 忙） |
| 窗 | 焦点 / 非焦点 |
| Files | 空目录 / Store 托管只读提示 |
| Store | 无选中 / 未装 / 已装 / 作业中 |

### 4.4 不做

任意边停靠、可拖分隔条、动画展开侧栏、卡片瀑布流、圆角窗框（控件圆角沿用现 `Ui*RoundRectangle` 即可）。

---

## 五、方向 3 · 汉字（先全，再好看）

### 5.1 缺字为什么「抽了」

```c
/* VideoText.c */
if (Cp >= 128 && !FontGlyphCp(Cp, 0, 0)) {
    return;   /* 缺字形：不画、不占位 → 中文句子挖空 */
}
```

`LocaleTable.c` 的 `gZhFallback` 已写全套中文，但 `cjk16` 仍是 I18N1 教学子集。例如 zh 有「商店 / 设备 / 贪吃蛇 / 缩放 / 经典 / 科技 / 已保存 / 重启 / 文件夹」等，库里没有对应码点。

### 5.2 覆盖集（验收用，可脚本扫）

1. 全部 `gZhFallback[]` + 本柱新 `MSG_*` 的 UTF-8 码点（CJK + 全角标点）。  
2. 桌面/开始菜单会显示的应用短名（与 MSG 重复则去重）。  
3. **不**扫用户文件名、不扫网卡 SSID（那些缺字画框即可）。

预估 / 现状：默认 **GB2312 全表**（约 **7445** 码点）∪ Locale/源码/课用词；字形 **18×18×4bpp**（16+2）。生成：`python3 Tools/Scripts/gen-cjk32.py`（默认 `--bpp 4 --dim 18`）。

### 5.3 观感（18×18 居中 · 4bpp 灰度）

- **NUC**：32/24 偏大；现行 **18×18**（曾 16 略小 +2），相对 Terminus 行高**垂直居中**；已关 Stretch 拉高。  
- **清晰度**：英文 = Terminus 手调 1bpp；汉字 = Noto **4bpp**（nibble→alpha=`n*17`）。灰度点阵 ≈ 宿主栅格一次 + 运行时 alpha；**TTF** 要进 FreeType/字体文件/字形缓存，体积与依赖差一个数量级，仍 D.9 默认不做。  
- 重跑：`python3 Tools/Scripts/gen-cjk32.py`（`--dim 18 --bpp 4`）。

### 5.4 缺字回退

`FontGlyphCp` 失败时画 **空心方框**（宽 = 汉字步进），禁止静默吞字符。Shell `test glyph` / `lang zh` 一眼能看出还有谁没进库。

### 5.5 文案路径

Settings / Files / Store / Devices / Desktop **客户区可见字符串**一律 `LocStr`。现在的 `"Detail"`、`"Click item to apply"`、`"Esc = back to list"`、`"id:"` 等算 **i18n 债**，单独一刀清。新中文必须先入覆盖集再进界面。

---

## 六、PR 切分（JX 只认路线图 ★）

顺序：**字能显示 → 灰度好看 → 文案走表 → 换默认色 → 按令牌改布局**。配色不依赖布局；布局令牌一刀先落地 Settings，别的程序抄。

| 序 | PR | 做 | 不做 | 验收 |
| -- | -- | -- | ---- | ---- |
| 0 | **PR-UI-doc** | 本文 + 路线图柱 | 改 Theme/Font 代码 | 文首能 JX 下一刀 |
| 1 | **PR-UI-cjk-cover** | **GB2312 全表**点阵 + 缺字画框；菜单换语言重建 | TTF | **实现齐**：`lang zh` 无吞字；库外见框 |
| 2 | **PR-UI-cjk-face** | **16×16** 原生格、不拉伸 | TTF | **实现齐**：字号合适、不糊 |
| 2b | **PR-UI-cjk-gray** | **18×18×4bpp** + `PaintGlyph4` | TTF；主题开关 | 中文边缘软于 1bpp |
| 3 | **PR-UI-i18n** | 三栏 UI 硬编码英文 → `MSG_*` + zh/en；新字并入覆盖 | 改几何 | `lang zh` 无详情栏英语残渣（专名/快捷键 `Esc` 可留） |
| 4 | **PR-UI-palette** | `theme=modern` 色表 §3.2；出厂默认 modern；经典=旧 default；tech 冻结；modern 标题渐变关 | 新特效；改 tech 色 | 冷启动即浅色现代；切经典/tech 不毁 |
| 5 | **PR-UI-layout-set** | 令牌 + **只改 Settings** 三分栏/行高/详情空态/色块横排 | Files/Store | 1280×720 Settings 不像调试器 |
| 6 | **PR-UI-layout-apps** | Files + Store（+ Devices 若仍纯堆字）套同一令牌 | 桌面 | 三程序侧栏同宽、底栏同高、空态一致 |
| 7 | **PR-UI-layout-desk** | 图标间距、开始菜单分组与行高 | 新壁纸资源（可用现 WALL.BMP） | 桌面不挤；开始菜单能扫 |

**合刀禁令**：palette 不跟 cjk 揉；layout-apps 不跟 palette 揉；cjk-cover 不跟 i18n 新文案抢（i18n 若新增汉字，收口必须再跑覆盖脚本或并进同一提交的生成结果）。

**建议 JX 序**：0（若未入库）→ 1 → 2 → 2b → 3 → 4 → 5 → 6 → 7。

---

## 七、给实现的路径速查

| 件 | 路径 |
| -- | ---- |
| 主题 | `Include/Theme.h`；`Common/Services/Theme/Theme*.c`；`ThemeTech.c` |
| 设置 | `Common/Services/SettingsUi/` |
| 文件 | `Common/Services/FilesUi/` |
| 商店 | `Common/Services/StoreUi/` |
| 桌面 | `Common/Services/Desktop/` |
| 文案 | `Common/Services/Locale/LocaleTable.c` |
| 汉字 | `Common/Fonts/cjk32.c`；`FontCjk32Lookup` / `FontCjkBitsPerPixel`；`VideoGlyph` |
| 控件 | `Common/Library/UI.c`；`Include/UI.h` |

生成点阵：宿主 `Tools/Scripts/gen-cjk32.py`（Noto 栅格）；生成物提交进仓库，Guest **不**跑 Python。

---

## 八、明确不做（本柱边界）

| 项 | 原因 |
| -- | ---- |
| 再做一套 tech / 赛博描边 | 已证不好看 |
| 把 effects 默认调到 medium/high | 特效不如默认 |
| TTF / 完整 Unicode | D.9；灰度点阵已开，矢量仍不做 |
| 窗口圆角 chrome | 美化柱已取消 |
| 用户文件名 / 任意作文全 Unicode | 默认到 **GB2312**；超集另开刀 |

柱收官 = §六 1–7 ✅（含 2b gray）且 QEMU `lang zh` 主路径无吞字、默认主题为 modern、Settings/Files/Store 同一套令牌。
