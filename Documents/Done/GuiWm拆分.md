# 任务：拆分 GuiWm.c（PR-S-guiwm-split-1 / -2）

> **规格**：只搬家；`Gui.h` / 现有 `GuiPriv.h` 不动语义；每刀 build + smoke。  
> **★ 下一刀**：轨 F 收官后见路线图文首（**compose-split-1**）。  
> **统计**：2026-09-16；split-1 TG `304d076`；split-2 TG `d11e78c`。

### ★ 进度

| 状态 | PR | 内容 |
| --- | --- | --- |
| ✅ `304d076` | **PR-S-guiwm-split-1** | `GuiHit.c`：WinCopy / PointIn* / Raise / GuiRaiseToFront |
| ✅ `d11e78c` | **PR-S-guiwm-split-2** | `GuiOpen.c`（Close/Place/Open*）；`GuiPointer.c`（Resize/Click/Mouse） |

**行数**：`GuiWm.c` ~179（全局/Init/标题）；`GuiOpen.c` ~486；`GuiPointer.c` ~477；`GuiHit.c` ~139。

**说明**：文中「resize」= `GuiOnDisplayResize`（分辨率钳窗），非用户拖角改窗。
