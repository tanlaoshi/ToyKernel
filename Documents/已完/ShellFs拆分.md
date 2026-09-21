# 任务：拆分 ShellCommandsFs.c（PR-S-shellfs-split-1）

> **规格**：只搬家；`ShellCommands.h` 对外语义不变；每刀 build + smoke。  
> **★ 下一刀**：见路线图文首（**排队已空**）。  
> **统计**：2026-09-16；split-1 TG `45c28e3`。

### ★ 进度

| 状态 | PR | 内容 |
| --- | --- | --- |
| ✅ `45c28e3` | **PR-S-shellfs-split-1** | `ShellCmdInstall.c`：`CommandInstall` + helpers + `ShellCmdInstallRegister` |

**行数**：`ShellCommandsFs.c` ~443；`ShellCmdInstall.c` ~164。
