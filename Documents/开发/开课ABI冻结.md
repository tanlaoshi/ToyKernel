# 开课 ABI 冻结表（印刷 · 大纲夹页）

> **开课冻结日认本表**（2026-09-27）。数值以 `User/include/` 宏为准；破坏性改名须升 MAJOR，并改本表 + [`学生速查卡.md`](学生速查卡.md) + [`API速查.md`](API速查.md) + [`应用开发指南.md`](应用开发指南.md)。  
> SDK 包号另见 `Tools/Sdk/VERSION` = **`1.0.0-course`**（与下表 CRT/lib 号无关）。  
> 总入口：[`开发者接手指南.md`](开发者接手指南.md)。

| 组件 | 宏前缀 | 冻结版本 | 头文件 |
| ---- | ------ | -------- | ------ |
| CRT / libtoyos | `TOYOS_CRT_VERSION_*` | **1.4.0** | `toyos/version.h` |
| libToyGfx | `TOY_GFX_ABI_VERSION_*` | **1.3.0** | `ToyGfx.h` |
| libToyUi | `TOY_UI_ABI_VERSION_*` | **1.2.0** | `ToyUi.h` |
| libToyNet | `TOY_NET_ABI_VERSION_*` | **2.0.1** | `ToyNet.h` |
| libFsUtil | `FS_UTIL_ABI_VERSION_*` | **1.0.0** | `FsUtil.h` |

**课上承诺**：只升 MINOR / PATCH；不升破坏性 MAJOR（除非发新冻结修订并改讲义）。

**其它已钉（细节见接手指南）**：

| 项 | 钉死 |
| -- | ---- |
| 未知 syscall | `-ENOSYS`（**-38**）；日志最多 8 次 |
| `wait` | 非 `waitpid`；CRT `*status=(exit&0xff)<<8`；`WEXITSTATUS` |
| `fopen("w")` | **不截断** → 先 `remove` |
| GUI 号 | **1000+**（勿用 18/19/20/21） |
| `DamageRect` | 单次 ≤64×64 |
| 网络 | POSIX `connect`/`bind`+`sockaddr` **网络序**；`ToyNetConnect(fd,ip,port)` **主机序** |
| 任务快照 | `SYS_TASK_SNAP`=**1200**；`TOY_TASK_SNAP_VER`=**1**（`toyos/task.h`）；范例 `TASKMGR.ELF` |

**核对（开课前再跑一遍）**：

```bash
rg -n 'VERSION_STRING' User/include/toyos/version.h User/include/ToyGfx.h \
  User/include/ToyUi.h User/include/ToyNet.h User/include/FsUtil.h
```
