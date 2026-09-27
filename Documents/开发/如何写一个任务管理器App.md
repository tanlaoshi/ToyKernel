# 如何写一个任务管理器 App（端到端范例）

> **目标读者**：会 C、第一次在 ToyOS 上做带窗口的应用。  
> **成品**：`TASKMGR.ELF`（源码 `User/Apps/TaskMgr.c`）——窗内显示与 Shell `ps` **同源**的任务表，并附带空闲页 / CPU tick / Worker 循环等系统计数；可 Refresh、翻页、Kill 用户任务。  
> **配套**：通用步骤见 [`应用开发指南.md`](应用开发指南.md)；ABI 夹页 [`开课ABI冻结.md`](开课ABI冻结.md)；API 表 [`API速查.md`](API速查.md)。

本文按「想清楚 → 开接口 → 写 App → 编进盘 → 跑通 → 进菜单」顺序写，每步都对应仓库里的真实路径。

---

## 0. 你要做的事（一句话）

| 层 | 做什么 | 本范例落点 |
| -- | ------ | ---------- |
| **产品** | 用户可见的窗口程序 | 标题 `TaskMgr`，按钮 Refresh / Next / Kill |
| **数据** | 用户态读不到内核 `gTasks[]`，必须 syscall | `SYS_TASK_SNAP`（1200）+ `toyos/task.h` |
| **UI** | 链 libToyUi（内部再链 libToyGfx） | `ToyUiCreateWindow` / `SetLabel` / `AddList` / `AddButton` / `Poll` |
| **构建** | Makefile 出 ELF，`build.sh` 拷到 TOYOS 盘 | `Build/User/taskmgr.elf` → `TASKMGR.ELF` + `Apps/TASKMGR.ELF` |
| **发现** | 开始菜单 Apps / `exec` / 商店 catalog | 内置 catalog 行 `taskmgr\|…\|Task Manager` |

---

## 1. 先想清楚：能展示什么？

Shell 里敲 `ps`（实现：`Common/Services/ShellCommands/ShellCommandsSystem.c` → `CommandPs`）已经打印：

| 字段 | 含义 |
| ---- | ---- |
| `pid=` | **槽位 + 1**（与 `kill <pid>` 一致，不是 `getpid()` 的裸 `TASK.Id`） |
| `Name` | 任务名（`shell` / `gui` / `worker` / ELF 基名等） |
| `user` / `kern` | 用户态还是内核任务 |
| `zombie` / `blocked` | 状态（省略则视为可运行） |
| `root` / `rip` | 页表根、指令指针（调试向；App 里可省略以省 UI） |
| `ticks` / `cpu` / `home` / `prio` | 调度统计 |
| 末行 | `cpu ticks` / `worker loops` / `steals` |

**适合放进 App 的额外信息**（不必新开 syscall 也能凑）：

| 来源 | 字段 | 本范例 |
| ---- | ---- | ------ |
| 快照 syscall | `FreePages` | 摘要行 `free=…pg` |
| 快照 syscall | `CpuTicks` / `WorkerLoops` / `StealCount` | 结构里有；窗摘要优先页数+任务数 |
| CRT | `clock_ms()` / `getpid()` | 摘要 `t=…ms`；Kill 时对照 `SelfPid` |
| 已有 | `kill(pid, SIGKILL)` | 列表选中后 Kill（拒杀内核任务与自己） |

**刻意不做的**（避免一刀过大）：改优先级 UI、画 CPU 历史曲线、读 `/proc`（ToyOS 无此 VFS）。

---

## 2. 开课接口：新增 `SYS_TASK_SNAP`

用户态**禁止** `#include` `Include/Scheduler.h`。要把 `ps` 数据交给 App，只能：

1. 在 **`Include/SyscallABI.h`** 系统信息段占号（本范例 **`SYS_TASK_SNAP = 1200`**）。  
2. 定义 **稳定布局** 的用户头：`User/include/toyos/task.h`（`TOY_TASK_SNAP` / `TOY_TASK_ENTRY` + `toy_task_snap()`）。  
3. 内核实现：`SysTaskSnap`（`Core/Syscall/SyscallProc.c`），在 `SyscallDispatch` 里分发。  
4. **同步文档**：开课 ABI / API 速查 / 本指南（改字段须升 `TOY_TASK_SNAP_VER`）。

### 2.1 布局约定（教学钉死）

```text
TOY_TASK_SNAP
  Magic='TPS1'  Version=1  Count  FreePages
  CpuTicks  WorkerLoops  StealCount  SelfPid
  Tasks[16] → { Pid, Parent, Flags, Priority, OnCpu, HomeCpu, Ticks, Name[16] }

Flags: USER | ZOMBIE | BLOCKED | CURRENT
Pid / Parent / SelfPid：与 Shell ps、kill 相同（槽位+1）
```

内核侧有一份同名布局的 `TASK_SNAP`（`_Static_assert` 防膨胀）；`VirtualMemoryCopyToUser` 一次拷走。

### 2.2 调用形态

```c
#include <toyos/task.h>

TOY_TASK_SNAP Snap;
if (toy_task_snap(&Snap) != 0) { /* 失败 */ }
/* Snap.Magic == TOY_TASK_SNAP_MAGIC && Version == TOY_TASK_SNAP_VER */
```

`toy_task_snap` 是头文件内联：`toy_syscall(SYS_TASK_SNAP, buf, sizeof(*buf), 0)`。

---

## 3. 写 App：`User/Apps/TaskMgr.c`

### 3.1 依赖库怎么选

| 需求 | 库 | 头 |
| ---- | -- | -- |
| 窗口 / 按钮 / 列表 / 事件 | **libToyUi** | `<ToyUi.h>` |
| （Ui 依赖）文字 damage | libToyGfx | 一般不必直接 include |
| printf / snprintf | CRT libc | `<stdio.h>` |
| yield / clock / kill | CRT | `<sched.h>` `<unistd.h>` `<signal.h>` |
| 任务快照 | （无 .a，仅头+syscall） | `<toyos/task.h>` |

链接顺序（与 `GUIDEMO` 相同）：`taskmgr.o` → `libToyUi.a` → `libToyGfx.a` → CRT 目标。

### 3.2 UI 结构（受现有控件限制）

- **`ToyUiAddList` 目前只画白/蓝底框，不绘 `Items[]` 文字**（`ToyUiRedrawWin` 无字模 blit）。  
  本范例**不用 List**，改用 **`ToyUiSetLabel`（`DamageText`）**——客户区唯一可靠出字路径。  
- 文案用 **`\\n` 两行**（内核 `PaintUserClient` 认换行）：上行 pid/名/状态，下行 ticks/cpu/prio/free。  
- **Next**：`gIndex` 循环翻任务；**Refresh** 重拉快照；**Kill** 杀当前这条（拒内核/自己）。  
- 事件环：`ToyUiPoll`；无事件则 `sched_yield()`（勿空转占满核）。

### 3.3 安全与课堂演示点

- Kill 前检查：`TOY_TASK_F_USER`、不是 `SelfPid`。  
- USER 窗焦点下 `printf` **只走串口**（不灌其它 Shell）——调试日志用串口即可。  
- 关窗：`TOY_UI_EVENT_CLOSE` → `return 0`。

### 3.4 源码地图（读代码时按此跳）

| 函数 | 作用 |
| ---- | ---- |
| `Refresh` | syscall + `PaintPage` |
| `Paint` | `SetLabel` 显示当前 `gIndex` 任务 |
| `KillCurrent` | 当前任务 → `kill` |
| `main` | 开窗、按钮、事件环 |

---

## 4. 挂进构建系统

只改三处（对照仓库已提交写法）：

1. **`Makefile`**  
   - `USER_TASKMGR_OBJ` / `USER_TASKMGR_ELF`  
   - `all:` 依赖加上 `$(USER_TASKMGR_ELF)`  
   - 编译 / 链接规则：仿 `USER_GUIDEMO_*`（Ui+Gfx+CRT）

2. **`build.sh`（x86 同步盘）**  
   ```bash
   cp -f "$USER_OUT/taskmgr.elf" "$DEST/TASKMGR.ELF"
   mkdir -p "$DEST/Apps"
   cp -f "$USER_OUT/taskmgr.elf" "$DEST/Apps/TASKMGR.ELF"
   ```  
   - 根目录：方便 `exec TASKMGR.ELF`  
   - `Apps/`：开始菜单 **Apps** 二级会扫 `.ELF`（见 `DesktopMenuApps.c`）

3. **商店内置 catalog**（可选但推荐）  
   `StoreCatalog.c` Builtin 增加：  
   `taskmgr|app|1|TASKMGR.ELF|-|x86_64|Task Manager`  
   标题「Task Manager」会出现在 Apps 列表（并走根目录 / `Apps/` 解析，见 `StoreResolveAppPath`）。

**FAT 8.3**：盘上文件名保持大写 `TASKMGR.ELF`（≤8 字符基名）。

---

## 5. 编译、部署、验收

```bash
cd /path/to/edk2/ToyKernel
./build.sh x86_64
# 成功应看到 RootFs 同步日志，且存在：
#   Build/User/taskmgr.elf
#   ../ToyImage/RootFs/X64/TASKMGR.ELF
#   ../ToyImage/RootFs/X64/Apps/TASKMGR.ELF

cd ../ToyImage
./Scripts/run-split.sh --kill-qemu    # 或真机 TBU 刷 U 盘后再测
```

Guest：

```text
toyos> exec TASKMGR.ELF
```

或：开始菜单 → **Apps** → **Task Manager** / **TASKMGR**。

**验收清单**

1. 窗标题 `TaskMgr`，摘要含 `n=` / `free=` / `self=`。  
2. 标签行能看到 `shell` / `gui` / `worker` 等；点 **Next** 换下一条。  
3. 另开一个 `exec HELLO.ELF`（或再开 Shell），点 **Refresh** 后 `n=` / 序号分母变化。  
4. **Next** 到某个**用户**任务（非 TaskMgr 自己）→ **Kill** → Refresh 后该任务消失。  
5. 串口有 `taskmgr: wid=…` 日志；其它 Shell 窗不被刷屏。  
6. 对照：`toyos> ps` 与窗内 pid/名一致。

---

## 6. 和「课外 Pkg 模板」的关系

| 路径 | 何时用 |
| ---- | ------ |
| `User/Apps/TaskMgr.c` + 内核 `Makefile` | **仓库内示范 / 开课镜像自带**（本范例） |
| `User/Pkg/` + `make PROG=…` | 学生**课外**独立工程；需自备对 `SYS_TASK_SNAP` 的头（或用 SDK 打包后的 `toyos/task.h`） |
| `Tools/Sdk/` | 维护者 `./Tools/build-sdk.sh` 打 tar；示例目录可再加 `TaskMgr`（非必须） |

课堂讲「完整流程」时：**以仓库内 Apps 为准**；再告诉学生作业可拷 `User/Pkg`。

---

## 7. 你自己做下一个 App 时的检查单

1. **要不要新 syscall？** 只读已有 ABI（文件/窗/网络）→ 不必；要碰调度表/设备私有态 → 新号 + 用户头 + 文档三处同步。  
2. **UI 用 Ui 还是纯 Gfx？** 按钮/列表 → ToyUi；像素游戏 → ToyGfx（参考 `Snake.c` / `BlitDemo.c`）。  
3. **stdout 去哪？** 有 USER 窗时默认**串口**；要给人看的字写进 `SetLabel` / Gfx。  
4. **进菜单吗？** 拷到 `Apps/*.ELF` 或写 catalog / `si.*`。  
5. **三架构？** 新 syscall 在 `SyscallDispatch` 共用即可；用户 ELF 仍分盘（x86 主课用 `RootFs/X64`）。  
6. **命名**：源文件 PascalCase（`TaskMgr.c`），盘上 8.3（`TASKMGR.ELF`），见 [`开发命名规范.md`](开发命名规范.md)。  
7. **单文件 ≤300 行**；再长就拆 `TaskMgrPaint.c` 等。

---

## 8. 相关文件速查

| 角色 | 路径 |
| ---- | ---- |
| App | `User/Apps/TaskMgr.c` |
| 用户 ABI 头 | `User/include/toyos/task.h` |
| 号段 | `Include/SyscallABI.h`（`SYS_TASK_SNAP`） |
| 内核实现 | `Core/Syscall/SyscallProc.c` → `SysTaskSnap` |
| 分发 | `Core/Syscall/Syscall.c` |
| Shell 对照 | `ShellCommandsSystem.c` → `CommandPs` |
| 菜单扫盘 | `Desktop/DesktopMenuApps.c` |
| 构建 | `Makefile` / `build.sh` |

---

## 9. 常见问题

**Q：为什么不直接让 App `#include Scheduler.h`？**  
A：用户态没有内核符号与地址空间；链接也会失败。教学上强制走 syscall，和真实 OS 一致。

**Q：以前窗中间只有白条？**  
A：`AddList` 只填色块；文字未实现。范例已改为 `SetLabel` + Next 翻任务。要多行字需后续给 ToyUi/Gfx 加「定点字符串」或自绘字形。

**Q：`getpid()` 和列表里的 pid 不一样？**  
A：历史：`TASK.Id` 是槽位；`ps`/`kill`/本快照用 **槽位+1**。摘要里的 `self=` 已按快照的 `SelfPid`（+1）显示，与 Kill 一致。

**Q：真机如何更新？**  
A：`./build.sh` 后挂载 TOYOS，执行 `ToyImage/Scripts/sync-kernel-usb.sh`（协作暗号 **TBU**），或整盘同步策略见 `SYNC.md`。
