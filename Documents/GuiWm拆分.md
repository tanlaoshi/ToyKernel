# 任务：拆分 GuiWm.c（PR-S-guiwm-split-1 / -2）

> **规格**：只搬家；`Gui.h` / 现有 `GuiPriv.h` 不动语义；每刀 build + smoke。  
> **★ 下一刀**：**PR-S-guiwm-split-2**（开窗 / Close / 鼠标编排）。  
> **统计**：2026-09-16；split-1 TG `304d076`。

### ★ 进度

| 状态 | PR | 内容 |
| --- | --- | --- |
| ✅ `304d076` | **PR-S-guiwm-split-1** | `GuiHit.c`：WinCopy / PointIn* / Raise / GuiRaiseToFront |
| ← **JX** | **PR-S-guiwm-split-2** | Open* / CloseWindow / Place / 点击鼠标编排 |

**行数**：`GuiWm.c` ~1107；`GuiHit.c` ~139；`GuiFocus.c` 已有（焦点态，不本刀）。

**说明**：文中「resize」= `GuiOnDisplayResize`（分辨率钳窗），非用户拖角改窗。
