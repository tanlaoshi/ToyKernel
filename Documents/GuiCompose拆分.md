# 任务：拆分 GuiCompose.c（PR-S-compose-split-1）

> **规格**：只搬家；`Gui.h` / `GuiPriv.h` 不动语义；每刀 build + smoke。  
> **★ 下一刀**：见路线图文首（**console-split-2**）。  
> **统计**：2026-09-16；split-1 TG `053fa73`。

### ★ 进度

| 状态 | PR | 内容 |
| --- | --- | --- |
| ✅ `053fa73` | **PR-S-compose-split-1** | `GuiDraw.c`（DrawWindow*/chrome/遮挡）；`GuiBackup.c`（备份/采样） |

**行数**：`GuiCompose.c` ~228（Present/主题场景）；`GuiDraw.c` ~404；`GuiBackup.c` ~376。
