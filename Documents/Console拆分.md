# 任务：拆分 Console.c（PR-S-console-split-1 / -2）

> **规格**：只搬家；`Console.h` 对外语义不变；每刀 build + smoke。  
> **★ 下一刀**：见路线图文首（**shellfs-split-1**）。  
> **统计**：2026-09-16；split-1 `c28b903`；split-2 TG `0dc7c72`。

### ★ 进度

| 状态 | PR | 内容 |
| --- | --- | --- |
| ✅ `c28b903` | **PR-S-console-split-1** | `ConsoleScroll.c` + `ConsolePriv.h` |
| ✅ `0dc7c72` | **PR-S-console-split-2** | `ConsoleCmd.c`：Register / 别名 / help / RunLine |

**行数**：`Console.c` ~569（绘制/提示符/输入）；`ConsoleCmd.c` ~695；`ConsoleScroll.c` ~222。
