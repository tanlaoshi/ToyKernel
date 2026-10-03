# igpu 优化空间（从 1 到 1.5）

> 状态：分析（**对照 2026-10 代码**，非只读旧规格）。  
> 配套：[`Intel核显2D-blit.md`](Intel核显2D-blit.md)、技术手册 §I、NUC NOTES。  
> 目标：柱已 0→1；本文按**现码**列 1→1.5，拆成可独立开的 PR。  
> 不改路线图 ★；开课前不做代码。

权威路径：`CodeA-HAL/X64/Drivers/Igpu/`。

---

## 1. 代码现状（对照旧规格）

| 旧规格说法 | 现码 | 结论 |
| ---------- | ---- | ---- |
| Present 可能还是 XY_COLOR_BLT | `IgpuPresent.c`：`IGPU_XY_SRC_COPY`（0x53）10 dword | **已是 blit，不是 fill** |
| 后缓冲未进 GTT | `IgpuBackMap` 写 `IgpuGsmMapQuiet(0x04000000+…)` + `IgpuGsmFlush` | **PTE 已写** |
| igpu-2 禁止写 PTE | 只约束 igpu-2；igpu-4 已写后缓冲 PTE | 勿再当 P0 |
| 翻页未接 | `IgpuScanout.c` 双缓冲 + `WaitVblank` + 改 `PLANE_SURF` **已写** | **从未被调用**（`IgpuScanoutInit` 无调用点） |
| COLOR_BLT 是 Present | `IgpuBlitTest.c` scratch / 右上角色块自检 | 自检用，勿改成 Present |
| batch 暂搁 | `IgpuBlitTest.c` B13：B12 `BB_START` 后 HEAD 卡死 | 仍真 |
| GPU Present 已挂钩 | `HalIgpuReady` = `IgpuReady() && IgpuPresentCopyOk()` | **CopyOk 恒 0** → 活路径仍是 `PresentRectRows` 按行 `memcpy` |

活路径（2026-10）：

1. `IgpuPresentPrepare` 会 `IgpuBackMap`，但 **不置** `gPresentCopyOk`（PR-G-igpu-corner：去掉右上角品红探针，避免屏上测色块）。
2. 日志：`igpu present cpu fallback (no probe)`。
3. `VideoPresentRect.c`：`HalIgpuReady()` 假 → 不进 `HalIgpuPresentRect` → **CPU memcpy**。
4. `IgpuSrcCopyRect` / `IgpuScanoutPresent` 代码在，门禁过不去。

因此 1.5 的缺口不是「再写一套 blit」，而是 **把门打开，并把已写死的翻页接上**。

---

## 2. 进步空间（按现码）

### 2.1 屏外探针 → 置 `PresentCopyOk`（真 P0）

- 缺的是门禁，不是命令。
- 旧品红探针改 scanout，黑屏只剩光标风险高；应 **scratch 页 SRC_COPY**（仿 `SubmitMemTest`：写 PTE、发 0x53、CPU 读回像素），成功再 `gPresentCopyOk=1`。
- 通过后 `PresentRectRows` 大矩形走 `IgpuPresentRect` → 脏矩形 `IgpuSrcCopyRect(BackGtt → gtt0)`。
- **收益**：高（memcpy 从热路径消失）。
- **风险**：中（误开仍可能黑屏；须失败软退）。
- **是否碰 ABI**：否。
- **是否碰 Present 语义**：是（内部从 CPU 拷改为 GPU 拷；对外仍是脏矩形 Present）。

### 2.2 接通 `IgpuScanoutInit`（翻页真正跑）

- `IgpuPresentRect` 已优先 `IgpuScanoutOk() && IgpuScanoutPresent`。
- `IgpuScanoutInit` **全树无调用**，`gScanOk` 永假 → 翻页死代码。
- 应在 CopyOk 之后、Prepare 里调一次；失败保持脏矩形 SRC_COPY。
- **收益**：中（整屏翻页消撕边，igpu-5 设计落地）。
- **风险**：中（改 `PLANE_SURF` / `HalVideoSetScanout`）。
- **是否碰 ABI**：否。
- **是否碰 Present 语义**：是（大矩形从直写 gtt0 改为隐缓冲+翻页）。

### 2.3 batch buffer（BB_START）

- 现码逐条 `IgpuBlitEmit` → ring。
- B12 编码曾错（IPEHR 垃圾）；通了可一帧多 blit 一次提交。
- **收益**：中（仅 GPU Present 热了之后才有意义）。
- **风险**：中。
- **是否碰 ABI**：否。
- **是否碰 Present 语义**：否。

### 2.4 硬件光标

- 另柱。`CUR_CTL` / `CUR_POS` / `CUR_BASE`；不占 CPU、不触发 Present。
- **收益**：中。
- **风险**：中。
- **是否碰 ABI**：否。
- **是否碰 Present 语义**：否。

### 2.5 更严 vsync

- `IgpuScanout.c` 已 `WaitVblank`（PIPEA `FRMCOUNT`）再改 SURF。
- 可再收紧到 vblank 窗口内翻页。QEMU 无真 vsync。
- **收益**：小。
- **风险**：低。
- **是否碰 ABI**：否。
- **是否碰 Present 语义**：是（仅翻页时序）。

### 2.6 明确不做

- 再实现一遍 XY_SRC_COPY / 再写一遍 `IgpuBackMap`。
- modeset、多代 i915、独显。

---

## 3. 可拆 PR（一刀一事）

> 新文件 ≤300；只进 HAL；失败软退；不改 `ToyUi.h`/`ToyGfx.h` 语义。  
> 未测勿 ✅。QEMU 无此卡 → 永远 CPU，验收在 **NUC**。

| PR | 一句话 | 依赖 | 主要改 | 预估 | 破 ABI | Present 语义 |
| -- | ------ | ---- | ------ | ---- | ------ | ------------ |
| **PR-G-igpu-6** | 屏外 SRC_COPY 探针，成功置 `PresentCopyOk` | 无（复用现 `IgpuSrcCopyRect`/`GsmMap`） | `IgpuPresentPrep.c`（+scratch 探针，勿画 scanout） | 80–150 | 否 | 是：热路径 memcpy→脏矩形 GPU |
| **PR-G-igpu-7** | Prepare 里调 `IgpuScanoutInit` | **必须 6**（Init 也查 CopyOk） | `IgpuPresentPrep.c` 一行级接通；必要时扫 `IgpuScanout.c` 软退 | 40–80 | 否 | 是：优先整屏翻页 |
| **PR-G-igpu-8** | ring 上 BB_START 打包多条 blit | 建议 6 之后（否则无热路径） | `IgpuBlit.c` / 测试；B12 教训 | 150–250 | 否 | 否 |
| **PR-G-igpu-9** | 硬件光标 plane | 无（另柱） | 新 `IgpuCursor*`；不改 Present 命令 | 200–300 | 否 | 否 |
| **PR-G-igpu-10** | 翻页等更严 vblank | **必须 7** | `IgpuScanout.c` `WaitVblank` | ≤80 | 否 | 是：时序 |

**不要拆进这些 PR 的**：重写 0x53 包、重写 GTT 后缓冲、COLOR_BLT 改 Present、modeset。

**开课前**：全部不做。  
**开课后若做**：先 **6**，NUC 手测通过再考虑 **7**；8/9/10 不自动进 ★。

---

## 4. 值不值得做（按现码）

标准仍是：**热路径还有 O(像素) memcpy 吗？**

| 观察 | 含义 |
| ---- | ---- |
| 串口 `cpu fallback (no probe)` 且无 `present copy ok` | 现态；**6 值得做** |
| 6 之后拖窗仍「两侧从上往下」细边 | 脏矩形直写 gtt0；**7 值得做** |
| 6+7 之后拖窗已顺、CPU 不顶满 | P0 收；再谈 8/9 |
| 6 失败（探针 miss） | 保持 memcpy；**禁止硬开 CopyOk** |

---

## 5. 建议

### 5.1 开课前

不做。门禁是故意关的（防黑屏只剩光标）。

### 5.2 开课后第一刀 = 只做 igpu-6

1. scratch 上 SRC_COPY，CPU 读回，勿写 GOP。
2. 成功：`gPresentCopyOk=1`，打 `present copy ok`。
3. 失败：CopyOk=0，桌面 memcpy。
4. NUC：拖窗 / 开窗 / 关窗 / Theme；对比串口是否还走 fallback。
5. **不要**在 6 里顺手 `ScanoutInit`（翻页失败会把 6 的验收搅浑）。

### 5.3 第二刀 = igpu-7（可选）

CopyOk 稳定后再接通翻页。失败则仍脏矩形 SRC_COPY。

### 5.4 不要做的

- 把 COLOR_BLT 换成 SRC_COPY 当 Present（已经是 SRC_COPY）。
- 再实现 `IgpuBackMap`。
- 无探针直接 `CopyOk=1`。
- modeset / 多代 i915 / 独显。

---

## 6. 一句话

码已经能 GPU blit（SRC_COPY + 后缓冲 PTE + 翻页函数）；活桌面仍是 memcpy，因为 **CopyOk 不开** 且 **ScanoutInit 没接**。  
1.5 拆成 **6 开门 → 7 翻页 → 8 batch / 9 光标 / 10 vsync**。开课前碰都不碰。

---

## 7. 参考

- [`Intel核显2D-blit.md`](Intel核显2D-blit.md)（本柱；§13 指针）
- `IgpuPresent.c` / `IgpuPresentPrep.c` / `IgpuScanout.c` / `VideoPresentRect.c`
- [`../技术手册.md`](../技术手册.md) §I
- [`../../CodeA-HAL/X64/NOTES-UEFI-PC.md`](../../CodeA-HAL/X64/NOTES-UEFI-PC.md)
- [`开机流程与加速.md`](开机流程与加速.md)

---

## 进度

- **2026-10-04**：`PR-G-igpu-6` JX — `IgpuPresentPrep.c` 屏外 SRC_COPY 探针（两页 scratch，不写 GOP）；成功 `gPresentCopyOk=1`。不接 `IgpuScanoutInit`。待 NUC 手测 TG。
- **2026-10-04**：`PR-G-igpu-6` ✅ TG — NUC `present copy ok`；探针改独立 GTT `0x05800000`（勿复用 scratch 品红页）。`ScanoutInit` 仍不调用。
- **2026-10-04**：`PR-G-igpu-7` ★ JX — `IgpuPresentPrepare` CopyOk 后 `IgpuScanoutInit`；失败软退脏矩形。待 NUC `scanout flip ready`。
- **2026-10-04**：`PR-G-igpu-7` ✅ TG — NUC `present copy ok` + `scanout flip ready`。
- **2026-10-04**：Settings 悬停左右栏频闪 ✅ — 悬停只重绘行；live 后小脏区直写当前 scanout。
