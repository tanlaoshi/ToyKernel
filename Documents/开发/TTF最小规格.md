# TTF 最小规格（评估用）

> 状态：分析 + **ttf-fpu / 0 / 1 / 2 / 3 ✅ TG**；最小柱收口（`ttf-4` 不做）。  
> 目标：若要「运行时 TrueType」汉字，最小可落地长什么样、拆哪些 PR、卡在哪。  
> **不改路线图 ★ 条目名以外的排期**；D.9 默认仍是点阵。TTF 柱开课后按本文 PR 表。  
> 配套：[`UI颜值与布局.md`](UI颜值与布局.md) §5、技术手册字体节、[`Nuklear学习与ToyUi深化.md`](Nuklear学习与ToyUi深化.md)（stb 烘焙 ≠ 内核栅格）。

权威路径：`Include/Library/Font.h`、`CodeB-Library/Fonts/`、`CodeA-HAL/X64/Drivers/Video/VideoGlyph.c`。

---

## 1. 代码现状

| 项 | 现码 |
| -- | ---- |
| ASCII | Terminus **1bpp**（`FONT_FACE`），硬边 |
| 汉字 | `cjk32.c`：**18×18×4bpp**，`FontGlyphCp` → `PaintGlyph4`（`alpha=n*17`） |
| 覆盖 | GB2312 量级（约 7k 码点）链进 **Kernel.elf**（约 7k×162 B ≈ **1.2 MB** 数据） |
| 拉伸 | `FontGlyphStretch` 只乘 `Face->Scale`，不再 16→32 拉高 |
| 外置字 | **T3**：`Assets/Fonts/*.FNT` **TOYF 点阵**，不是 TTF |
| 绘制入口 | `VideoDrawCodepointAt`：`Cp<128` 走 Terminus，否则 `FontGlyphCp` |
| 布局 | `FontCodepointAdvance`：汉字宽 ≈ `max(字形宽×Scale, FontAdvanceX())`；行高 `FontCellH()` / `UiLayoutRowH()` |
| 内核浮点 | x86/arm64 **` -mgeneral-regs-only`**；未开 `CR4.OSFXSR` 时 SSE **#UD** |

发虚主因是 **18px 灰度 AA**，不是缺 TTF 文件。`gen-cjk32.py` **宿主已经在用 Noto TTF 栅点阵**。

---

## 2. 最小产品（若做运行时 TTF）

一句话：**只换汉字后端**；ASCII / Theme / ToyUi ABI / 等宽格子 **不动**。

| 钉死 | 理由 |
| ---- | ---- |
| 只服务 `Cp ≥ 128` 的 `FontGlyphCp` | 英文继续 Terminus，避免两套 AA 混排 |
| 字库放 **RootFs** `Assets/Fonts/`（如 `CJK.TTF` 子集），**不链进 ELF** | 体积；T3 已有扫目录习惯 |
| 栅格尺寸 = 现 `FontCjkDim()`（18） | 行高/命中/侧栏不用重排 |
| 汉字 **步进仍用格子宽**（`FontCjkDim()*Scale`），字形在格内居中 | 变宽会打 Settings/菜单命中 |
| 缺文件 / 栅失败 → **仍走 `cjk32.c`** | 软退；QEMU 镜像忘拷 TTF 不黑字 |
| 缓存：8bpp 灰位图，槽数有上限（建议 **256～512**，约 100～200 KB） | 禁止按 GB2312 一次栅完 |
| 禁止在光标/IRQ 路径首次栅格 | 开始菜单已有 4bpp 预热教训 |
| 覆盖目标 = **现 UI 汉字 ∪ GB2312 子集文件**；不是完整 Unicode | 文件名生僻字可继续空心框 |

**明确不做（本最小柱）**：FreeType、HarfBuzz、kerning、ligature、ClearType/子像素、完整 Unicode、把 Noto 全量 TTC 丢进镜像、改 `ToyUi.h`/`ToyGfx.h`、为 TTF 改窗口布局令牌。

---

## 3. 最大暗礁：内核没有 FPU

`stb_truetype` 大量 `float`。当前内核 **禁止生成 XMM**。直接 `#include stb` 进 `Font*.c` → 真机/QEMU 都可能 **#UD**。

运行时 TTF 先过这一关，三选一（评估时就定，不要拖到挂钩绘制）：

| 方案 | 含义 | 代价 |
| ---- | ---- | ---- |
| **A. 宿主栅格（推荐若只为锐利）** | 继续 `gen-cjk32.py`，收边/Bold/1bpp；Guest **不**解析 TTF | **不是**运行时 TTF；1 刀级 |
| **B. FPU 岛** | 开 OSFXSR（或定点软浮点 TU），仅 Font 栅格允许 float | 中断进出要 save/restore；三架构都要审；易成独立柱 |
| **C. 整数栅格器** | 不引入 stb；自写/找定点 TTF 解析 | 工期 ≈ 再写一个小 FreeType；不最小 |

**评估结论预填**：若不能接受 B 或 C，**不要开运行时 TTF PR**；用 A（或 `PaintGlyph4` 收边）。

---

## 4. 体积与收益（量级）

| | 现在点阵 | 最小运行时 TTF |
| - | -------- | -------------- |
| Kernel.elf 汉字 | ~1.2 MB `cjk32` | 可暂留作 fallback（总 ELF 不降） |
| RootFs | TOYF 小样本 | 子集 TTF **约 2～8 MB**（全量 Noto CJK 十几～三十 MB，**禁止当最小**） |
| 首次出字 | 查表 | 栅格 + 填缓存（菜单要预热） |
| 18px 锐利 | 4bpp 软边 | stb **几乎无 hint**，只比点阵 AA **略自然**；要「印刷锐」仍要 FreeType hint（柱外） |
| 任意字号 | 否 | 最小规格 **先锁 18px**；多号另柱 |

---

## 5. 可拆 PR（一刀一事）

> 新文件 ≤300；失败软退；不改 `ToyUi.h`/`ToyGfx.h` 语义。  
> **未测勿 ✅**。QEMU + NUC 都要能软退。  
> **不自动进 ★**。若开课：必须先过 **ttf-fpu** 决策。

| PR | 一句话 | 依赖 | 主要改 | 预估行 | 破 ABI | 建议 |
| -- | ------ | ---- | ------ | ------ | ------ | ---- |
| **PR-UI-cjk-crisp** | `PaintGlyph4` 淡灰丢、深灰实心 | 无 | `VideoGlyph.c` 十来行 | ≤40 | 否 | **锐利优先走这条**，与 TTF 无关 |
| **PR-UI-ttf-fpu** | 定 B 或 C，或宣布不做运行时 | 无 | 短文 + 若 B：FPU 岛骨架（save/restore） | 80–250 | 否 | **门闩**；过不了后面全停 |
| **PR-UI-ttf-0** | 启动读 `Assets/Fonts/CJK.TTF`，校验 sfnt，打 log | T3 路径 | `FontTtfLoad.c`；`prepare-rootfs` 拷子集 | 100–200 | 否 | 无 FPU 也能做；不绘制 |
| **PR-UI-ttf-1** | 栅一号 18px + 定长缓存 | **fpu** + 0 | `FontTtfRaster.c` / `FontTtfCache.c` | 200–300×2 | 否 | 单测：`test glyph` 几个码点 |
| **PR-UI-ttf-2** | `FontGlyphCp` 优先缓存，失败 `cjk32` | 1 | `FontRegistry.c` 挂钩；`PaintGlyph4` 可接 8bpp | 80–150 | 否 | 验收：`lang zh` 桌面/Settings |
| **PR-UI-ttf-3** | 预热 Locale zh ∪ 开始菜单码点 | 2 | Desktop 预热扩到 TTF | ≤80 | 否 | 防首开卡 |
| **PR-UI-ttf-4** | （可选）ELF 去掉 `cjk32`，仅 TTF | 2+3 且镜像必有 TTF | 链接与 fallback | 50–100 | 否 | 即手册 **T4**；最小柱 **不做** |

**不要拆进这些 PR 的**：FreeType、完整 Unicode、ASCII 也改 TTF、变宽步进、Settings 新「TTF 开关」大页、modeset。

宿主子集脚本（`fonttools` pyftsubset，按现 `gCjk32Cp` + Locale）可附在 **ttf-0**，不算独立刀。

---

## 6. 建议开课顺序（若评估通过）

```text
先：cjk-crisp（或不做字）
若仍要运行时 TTF：
  ttf-fpu 决策 → 0 读文件 → 1 栅+缓存 → 2 挂钩 → 3 预热
  ttf-4 卸点阵：观察一周再谈
```

NUC 手测：`lang zh` 开始菜单 / Settings / Files 侧栏；对比现 4bpp 是否真锐。若「还是虚」→ **停**，不要加 hint 柱。

---

## 7. 值不值得（评估框）

| 观察 | 含义 |
| ---- | ---- |
| 只是 18px 灰边发虚 | **cjk-crisp / 重跑 gen-cjk32** 即可；不要 TTF |
| 要缺字更少、同一文件多号 | 才谈运行时 TTF；最小规格仍锁 18px |
| 要 Windows 级小字锐利 | 最小规格 **达不到**；那是 FreeType+hint，政策不做 |
| 不愿开 FPU 岛 | 运行时 TTF **不做** |

---

## 8. 一句话

现码汉字已经是「宿主 TTF → 点阵」；Guest 再解析 TTF 的最小柱 = **外置子集 + 缓存 + 挂钩 `FontGlyphCp`**，但被 **内核无 FPU** 卡住。评估先过门闩；过不了就只做点阵收边。

---

## 9. 参考

- `Font.h` / `FontRegistry.c` / `cjk32.c` / `VideoGlyph.c`
- `gen-cjk32.py`（宿主 Noto）
- 技术手册 Assets/Fonts（T3）与 **T4 可选外置大字库**
- [`../路线图.md`](../路线图.md) D.9：TTF 非默认

---

## 进度

- **2026-10-04**：规格落地（评估）。未进 ★、未写代码。
- **2026-10-04**：`PR-UI-cjk-crisp` ★ JX — `VideoGlyph.c` `GlyphCrispAlpha4`；待 NUC 汉字边缘。
- **2026-10-04**：`PR-UI-cjk-crisp` ✅ TG — NUC 略改善；仍是 18px 点阵上限。
- **2026-10-04**：`PR-UI-ttf-fpu` ★ JX — 方案 B：x86 OSFXSR 岛；Arm/RiscV 桩。待 `Boot: fpu island ok`。
- **2026-10-04**：`PR-UI-ttf-fpu` ✅ TG — NUC `Boot: fpu island ok`。下一刀 `ttf-0` 读 `CJK.TTF`，等 JX。
- **2026-10-04**：`PR-UI-ttf-0` ★ JX — `FontTtfLoad` + stub `CJK.TTF`；待 `Boot: ttf sfnt ok`。
- **2026-10-04**：`PR-UI-ttf-0` ✅ TG — NUC `Boot: ttf sfnt ok`。下一刀 `ttf-1` 栅 18px+缓存，等 JX。
- **2026-10-04**：`PR-UI-ttf-1` ★ JX — stb 18px + 256 槽；`test glyph` 打 `ttf U+… ok|miss`。待手测。
- **2026-10-04**：`PR-UI-ttf-1` ✅ TG — NUC `ttf sfnt ok` + `ttf init ok`。挂钩绘制仍是 `ttf-2`。
- **2026-10-04**：`PR-UI-ttf-2` ★ JX — `FontGlyphCp` 优先缓存；`PaintGlyph8`；失败 `cjk32`。待 `lang zh`。
- **2026-10-04**：`PR-UI-ttf-2` ✅ TG — 挂钩绘制；stub 仍 miss。下一刀 `ttf-3` 预热。
- **2026-10-04**：`PR-UI-ttf-3` ★ JX — Locale zh → Worker `LocaleTtfPreheatStep`；绘制只 Lookup。待 `lang zh` 不卡鼠。
- **2026-10-04**：`PR-UI-ttf-3` ✅ TG — Worker 分片 + Lookup；stub 仍点阵。最小柱完；真字库另说。
