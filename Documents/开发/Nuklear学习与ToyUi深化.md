# Nuklear 学习与 ToyUi 深化

> 状态：只读分析。不改代码。与 应用开发指南 §5.5、API速查 §GUI 配套。  
> 目标：从 Nuklear 借三样东西——立即模式接口设计、绘制命令缓冲、样式表——用于深化 ToyUi/ToyGfx。

---

## 1. Nuklear 是什么

- 单头文件、C89、零依赖、立即模式
- 核心 API 形状：`nk_begin` / `nk_layout_row_dynamic` / `nk_button_label` / `nk_end`
- 与 ToyUi 定位对比：

| 维度 | Nuklear | ToyUi |
| ---- | ------- | ----- |
| 形态 | 单头文件库 | 内核窗口协议 + C 库 |
| 绘制 | 自带命令缓冲 + 光栅化后端 | 内核 Gui 合成 |
| 输入 | 自带事件队列 | `ToyUiPoll` |
| 样式 | `nk_style` 数据结构 | 内核 Theme |
| 字体 | stb_truetype 烘焙 | 点阵 + TOYF |

（只读对照：`CodeE-User/include/Toy{Ui,Gfx}.h`、`CodeE-User/Library/Toy{Ui,Gfx}/`；内核侧 `CodeD-Services/{Gui,Desktop,Theme}/`。）

---

## 2. 三个可借鉴设计

### 2.1 立即模式接口：Begin/End + 数据驱动

代码形状：

```c
nk_begin(&Ctx, "Title", ...);
nk_layout_row_dynamic(&Ctx, 30, 1);
if (nk_button_label(&Ctx, "OK")) { ... }
nk_end(&Ctx);
```

为什么好用：

- 无控件对象树
- UI 是数据的函数
- 无回调注册

对 ToyUi 的启示：

- 现有 `ToyUiCreateWindow` / `AddButton` / `Poll` 是「半保留模式」
- 可加一层「立即模式薄封装」`ToyUiIm`
- 形状：

```c
ToyUiImBegin(Wid, "Title");
if (ToyUiImButton("OK")) { ... }
ToyUiImEnd();
```

- 实现：包现有 `ToyUiPoll` / `AddButton` / `SetLabel`；内部维护帧内控件列表

| 项 | 值 |
| -- | -- |
| 可行性 | **高** |
| 预估行数 | **200–400** |
| 是否破坏 ABI | **否**（新增 `ToyUiIm.h`，不改 `ToyUi.h`） |

### 2.2 绘制命令缓冲：`nk_command_buffer`

- Nuklear 把一帧内所有绘制收集成 `nk_command` 数组，最后一次性交给后端
- 好处：减少后端调用次数；便于脏区合并；便于静态层缓存

对 ToyUi/ToyGfx 的启示：

- ToyGfx 现在是「逐次 `DamageRect`」
- 可在用户态加一层命令缓冲 `ToyGfxBatch`
- 形状：

```c
ToyGfxBatchBegin(Wid);
ToyGfxBatchFillRect(...);
ToyGfxBatchDrawText(...);
ToyGfxBatchEnd();   /* 一帧末统一 DamageRect */
```

- 实现：内部收集命令，按 64×64 分块提交；合并相邻脏区

| 项 | 值 |
| -- | -- |
| 可行性 | **中** |
| 预估行数 | **300–500** |
| 是否破坏 ABI | **否**（新增 `ToyGfxBatch.h`） |
| 风险 | 与现有「单次 `DamageRect` ≤64×64」约束交互；需分块提交 |

### 2.3 样式表：`nk_style` + `nk_style_item`

- Nuklear 用纯数据结构描述按钮四态（默认/悬停/按下/禁用）的颜色、边框、圆角
- 与 ToyOS Theme 的对比：

| 维度 | 内核 Theme | Nuklear style |
| ---- | ---------- | ------------- |
| 管什么 | 窗口边框 / 桌面 / 任务栏 | 控件客户区 |
| 在哪 | 内核侧 | 用户态库内 |
| 可换否 | 编译期/DB | 运行时 |

对 ToyUi 的启示：

- 可加「用户态样式表」`ToyUiStyle`
- 形状：

```c
ToyUiStyle Style = ToyUiStyleDefault();
Style.ButtonHoverBg = 0x00406080;
ToyUiSetStyle(&Style);
```

- 实现：纯数据结构 + 控件绘制时查表

| 项 | 值 |
| -- | -- |
| 可行性 | **中** |
| 预估行数 | **200–300** |
| 是否破坏 ABI | **否**（新增 `ToyUiStyle.h`） |
| 风险 | 与内核 Theme 职责重叠；文档钉死「内核 Theme 管窗口边框/桌面，用户态 Style 管控件客户区」 |

---

## 3. Nuklear 不适合照搬的部分

| 部分 | 为什么不照搬 |
| ---- | ------------ |
| stb_truetype 字体烘焙 | ToyOS 用点阵 + TOYF，不引入 TTF |
| 多窗口管理 | ToyOS 由内核 Gui 管窗 |
| 自带后端（GL/D3D/SDL） | ToyOS 后端是内核窗口协议 |
| 完整 `nk_context` 状态机 | ToyUi 不需要这么重 |
| `nk_input` 事件队列 | ToyOS 由内核 Gui 投递 |

---

## 4. 对 ToyUi 的三条深化建议（按优先级）

### P0：立即模式薄封装（ToyUiIm）

- 形状：`ToyUiImBegin` / `ToyUiImButton` / `ToyUiImEnd`
- 实现：包现有 `ToyUiPoll` / `AddButton` / `SetLabel`
- 不改现有 `ToyUi.h`；新增 `ToyUiIm.h` + `ToyUiIm.c`
- 验收：一个 ToyUiIm 示例 ELF，与现有 GuiDemo 功能等价
- **可行性：高** · **预估：200–400 行** · **破 ABI：否** · 1–2 刀

### P1：用户态绘制命令缓冲（ToyGfxBatch）

- 形状：`ToyGfxBatchBegin` / `ToyGfxBatchFillRect` / `ToyGfxBatchEnd`
- 实现：内部收集命令，按 64×64 分块提交；合并相邻脏区
- 不改内核 ABI；只加用户态 Batch（不改既有 `ToyGfx.h` 语义）
- 验收：拖窗/滚动列表时 `DamageRect` 调用次数下降；帧率不变或更好
- **可行性：中** · **预估：300–500 行** · **破 ABI：否** · 2–3 刀
- 风险：与现有单次 `DamageRect` 约束交互，需分块；需真机 NUC 验拖窗

### P2：用户态样式表（ToyUiStyle）

- 形状：`ToyUiStyleDefault` / `ToyUiSetStyle`
- 实现：纯数据结构 + 控件绘制时查表；不改内核 Theme
- 验收：同一 App 不改内核即可换控件外观；与内核 Theme 不冲突
- **可行性：中** · **预估：200–300 行** · **破 ABI：否** · 1–2 刀

---

## 5. 明确不做

- 不把 Nuklear 源码放进仓库
- 不引入 stb_truetype / TTF
- 不把 ToyUi 改成另一套完整控件树
- 不把样式表塞进内核 Theme
- 不改现有 `ToyUi.h` / `ToyGfx.h` 的 ABI 语义

---

## 6. 给 Cursor 的后续 prompt 模板

- 「按 Nuklear §2.1 的思路，实现 `ToyUiIm.h`/`ToyUiIm.c`，只包现有 ToyUi，不改 ABI」
- 「按 Nuklear §2.2 的思路，实现 `ToyGfxBatch`，内部按 64×64 分块提交」
- 「按 Nuklear §2.3 的思路，实现 `ToyUiStyle`，纯用户态」

实现进度只更新本文 +（若用）`待做/进展-YYYY-MM.md`；路线图合并日再并。

---

## 7. 参考

- Nuklear 源码：`nuklear.h`（本地只读，路径：______；**不**入库）
- Dear ImGui：Begin/End 与 ID 栈
- 现有 `ToyUi.h` / `ToyGfx.h` / [`应用开发指南.md`](应用开发指南.md) §5.5 / [`API速查.md`](API速查.md)
