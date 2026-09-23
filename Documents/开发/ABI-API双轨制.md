# ABI / API 双轨制整理（分析稿）

> **状态**：网-1 / A / B / **C ✅ TG**（2026-09-23）。正文 §1–§10 是当时的分析快照。  
> **本次补充**：见下方「补充 POSIX」；**Syscall 号段**见「号段重排（SyscallABI）」——权威 [`SyscallABI.h`](../../Include/SyscallABI.h)。  
> **范围**：用户态 `User/include/` + CRT/lib。  
> **对照**：本文件相对任务书，已按**仓库实况**校正。

---

## 号段重排（SyscallABI · 2026-09-23）

ToyOS 未发布，syscall 号已按**段内双轨**重排（[`PR-U-syscall-abi`](../路线图.md#pr-u-syscall-abi)）：

| 规则 | 含义 |
| ---- | ---- |
| 大段 | 功能族，每段 100 号（0–99 进程、100–199 信号/调度、…） |
| ToyOS 子段 | 段内前 50（×00–×49）：Toy 独有 API |
| POSIX 子段 | 段内后 50（×50–×99）：POSIX 标准 API；两轨都有的归此 |
| 权威头 | 内核 `Include/SyscallABI.h`；用户 `toyos/syscall.h` `#include` 之 |
| 汇编 | `User/Apps/*.S` 用 `#include <toyos/syscall.h>` + `SYS_*`（`__ASSEMBLER__` 只暴露宏） |

**常用号（勿再写旧线性号 0/1/5/7…）**：

| 名 | 号 | 名 | 号 |
| -- | -- | -- | -- |
| `SYS_EXIT` | 50 | `SYS_WRITE` | 352 |
| `SYS_FORK` | 51 | `SYS_OPEN` | 350 |
| `SYS_WAIT` | 52 | `SYS_READ` | 351 |
| `SYS_GETPID` | 54 | `SYS_CLOSE` | 353 |
| `SYS_YIELD` | 150 | `SYS_FILE_STAT` | 400 |
| `SYS_SLEEP` | 750 | `SYS_GETCWD` | 550 |
| `SYS_CLOCK_MS` | 700 | `SYS_SOCKET` | 850 |

完整表见路线图 [`PR-U-syscall-abi`](../路线图.md#pr-u-syscall-abi) 号段速查。

---

## 补充 POSIX（2026-09-23 · 对照代码）

第 1 轨补 POSIX 名。第 2 轨（含 `ToyGfx*`、`FileStat`、`OpenDirectory`、`ToyNetConnect`）签名不动。一次只做 ★ 那一刀。

| 步 | 任务书 | 仓库实况 | 排刀 |
| -- | ------ | -------- | ---- |
| 1 `sched_yield` | 新 `sched.h`；`toy_yield` 改宏别名 | **TG**：`sched_yield` → `SYS_YIELD`（**150**）；`toy_yield` 为宏别名 | ✅ [`PR-U-sched-yield`](../路线图.md#pr-u-sched-yield) |
| 2 `getpid` / `getppid` | 任务书写号 30/31 | **TG**：`SYS_GETPID=54` / `SYS_GETPPID=55`；`proc.c` | ✅ [`PR-U-getpid`](../路线图.md#pr-u-getpid) |
| 3 `stat` / `fstat` | 样例把 `Attr` 原样写入 `st_mode` | `FileStat` → `SYS_FILE_STAT`（**400**）；`fstat` 规划 `SYS_FSTAT=451` | 排队 [`PR-U-stat`](../路线图.md#pr-u-stat) |
| 4 目录别名 | 样例用进程级 `static struct dirent` | **已落地**。`opendir`/`closedir` 是宏；`readdir` 缓冲在每个 `DIR` 里 | 不排刀 |
| 5 网络双轨 | `connect`→`ToyNetConnect`，POSIX `sockaddr` | **已落地**（ABI 2.0.1） | 不排刀 |

`accept` 仍是一参数，也没有 `inet_aton` / `inet_ntoa`。这两项不在本次五步里，不占 ★。

实现时：`sched_yield` 直接调 `toy_syscall(SYS_YIELD)`，不能再调用宏 `toy_yield()`。`getppid` 在 `ParentId==-1` 时返回 `0`。

---

## 0. 结论摘要（请先看）


| #   | 结论                                                                                                     |
| --- | ------------------------------------------------------------------------------------------------------ |
| 1   | 双轨目标与现有头文件布局**大体已分家**（POSIX 名在 `unistd`/`stdio`/…；Toy 名在 `ToyUi`/`ToyGfx`/`FsUtil`/`toyos/`）。          |
| 2   | **最大冲突在网络**：现有 `connect`/`bind` 已是 `(fd, ip, port)` **主机序**，**不是** POSIX `sockaddr`。同名无法再挂一套 POSIX 原型。 |
| 3   | 目录轨安全：`OpenDirectory` 等可保留；POSIX `opendir`/`closedir` 可用宏别名；`readdir` 需 CRT 包装（现无）。                    |
| 4   | 部分 A（只改速查）可独立落地；B/C 各需独立可编译刀，且 C 必须先定「谁占用 `connect` 这个名字」。                                             |
| 5   | 任务书里的 `ToyNetConnect` **目前不存在**；已有的是 `connect` + `ToyNetConnectIn`（结构体版）。                              |


---



## 1. 现有 `User/include/` 结构（实况）

```
User/include/
  ctype.h errno.h fcntl.h signal.h stdarg.h stddef.h
  stdio.h stdlib.h string.h unistd.h
  dirent.h          ← Toy 风格目录 + FileStat（非 POSIX 名）
  FsUtil.h          ← Toy 路径/列目录
  ToyUi.h ToyGfx.h ToyNet.h
  ToySyscall.h      ← 薄转发（旧）
  toyos.h
  toyos/syscall.h   ← toy_* 内联 + 号
  toyos/version.h   ← TOYOS_CRT_VERSION_*
  sys/types.h sys/mman.h
  （无 sys/socket.h）
```


| 轨倾向            | 头文件                                                                    | 备注                                                                                  |
| -------------- | ---------------------------------------------------------------------- | ----------------------------------------------------------------------------------- |
| 第 1 轨（POSIX/C） | `unistd` `fcntl` `stdio` `stdlib` `string` `signal` `errno` `sys/mman` | 名称已接近 POSIX；语义有教学简化                                                                 |
| 第 2 轨（Toy 特色）  | `ToyUi` `ToyGfx` `FsUtil` `toyos/*` `dirent`（全词名）                      | 符合命名规范 PascalCase / `Toy` 前缀                                                        |
| **混轨 / 债**     | `ToyNet.h`                                                             | `socket`/`connect`/`bind` 用 POSIX **名**，Toy **形参**                                  |
| **混轨 / 债**     | `unistd.h` 末尾                                                          | `create_window` / `damage` / `poll_input` / `ui_button`（旧 GUI syscall 名，非 `ToyUi`*） |


实现落点：


| 能力     | 实现                                                                                   |
| ------ | ------------------------------------------------------------------------------------ |
| 目录     | `User/crt/dirent.c` → `toy_open_directory` / `toy_read_directory`                    |
| 网络     | `User/Library/ToyNet/ToyNet.c` → `toy_socket` / `toy_connect` / `toy_bind`           |
| FsUtil | `User/Library/FsUtil/`                                                               |
| 演示     | `DirDemo.c` 只用 `OpenDirectory`；`NetLibDemo.c` 用 `connect(fd, ToyNetIpv4(...), port)` |


---



## 2. 现有 `dirent.h` / `dirent.c`

**头文件约定（刻意非 POSIX 名）**：

```c
TOY_DIR *OpenDirectory(const char *path);
int ReadDirectory(TOY_DIR *dir, TOY_DIR_ENT *out);  /* 1=有项 0=结束 -1=失败 */
int CloseDirectory(TOY_DIR *dir);
int FileStat(const char *path, TOY_FILE_STAT *out);
```

注释原文：「勿用孤立 `opendir`/`stat`」。

**实现要点**：

- `TOY_DIR` 仅包一个目录 fd；`malloc`/`free` 管理。
- `ReadDirectory` 把项写入调用方提供的 `TOY_DIR_ENT *`（**不是**返回静态 `struct dirent `*）。
- 无 `opendir` / `readdir` / `closedir`；无 `DIR` / `struct dirent` 类型别名。

**与 POSIX 差异（写进速查「注意」列用）**：


| 点    | ToyOS                 | POSIX             |
| ---- | --------------------- | ----------------- |
| 返回项  | 写入调用方缓冲               | `readdir` 返回内部指针  |
| 结束判断 | `ReadDirectory` 返回 0  | `readdir` 返回 NULL |
| 名长度  | `TOY_ENT_NAME_MAX` 64 | `d_name` 实现相关     |
| 属性   | `TOY_ATTR_*` / Size   | `d_type` 等        |


---



## 3. 现有 `ToyNet.h` / 网络实现

**已有（1.2.0）**：

```c
int socket(int domain, int type, int protocol);
int connect(int fd, unsigned ip, unsigned port);   /* 主机序 */
int bind(int fd, unsigned ip, unsigned port);      /* 主机序 */
int listen(int fd, int backlog);
int accept(int fd);                                /* 无 sockaddr 出参 */
ssize_t send(...); ssize_t recv(...);              /* 即 write/read */
ToySockAddrIn + ToyNetConnectIn / BindIn / GetAddrIn;
ToyNetIpv4 / ToyNetResolve;
```

**没有**：`sys/socket.h`、`struct sockaddr` / `sockaddr_in`、网络序 `htons`、POSIX 形参的 `connect`/`bind`。

**任务书 vs 实况**：


| 任务书                                  | 实况                       |
| ------------------------------------ | ------------------------ |
| 保留 `ToyNetConnect(fd,ip,port)`       | **无此名**；同语义函数叫 `connect` |
| 新增 POSIX `connect(fd, sockaddr*, …)` | 与现有 `connect` **C 符号冲突** |
| 内部 POSIX→ToyOS                       | 可行，但须先决定谁叫 `connect`     |


硬约束「不改现有 POSIX API 名称」在这里有歧义：  
现有符号 **名字像 POSIX，语义不是**。真要双轨，必须二选一（见 §7）。

---



## 4. 现有 `API速查.md` 结构

路径：`Documents/开发/API速查.md`（约 75 行）。

当前按主题混排，**未分双轨**：

1. 进程（含 `toy_yield`）
2. 文件（POSIX + `OpenDirectory` + `FsUtil*` 同节）
3. IPC / 内存
4. GUI（Toy）
5. 网络（ToyNet）
6. 常用 C
7. 已知缺口
8. 商店（内核服务）

已有「注意」列雏形（进程/文件/网络），部分表无注意列。

**过时一句**（与 rm-exc-11 不符，部分 A 应改）：

> Shell `store …` = **同步**且与 UI Job 互斥  

实况：Shell 与窗同为 **Enqueue → Worker**；Shell 立即回提示符；`store job` 查态。

---



## 5. 打算如何组织双轨制



### 5.1 原则


| 轨         | 名称策略                                                                   | 头文件策略                                                     |
| --------- | ---------------------------------------------------------------------- | --------------------------------------------------------- |
| **第 1 轨** | 保持 POSIX/C 习惯名；语义「尽量接近」并在速查写清差异                                        | 标准名头：`unistd.h` `stdio.h` …；网络 POSIX 进 `sys/socket.h`（新建） |
| **第 2 轨** | Toy 命名规范（`ToyUi`* / `ToyGfx*` / `FsUtil*` / `OpenDirectory` / `toy_*`） | `Toy*.h` `FsUtil.h` `dirent.h`（全词） `toyos/`               |




### 5.2 部分 A（仅文档）组织草案

```
# ToyOS API 速查

## 第 1 部分 · POSIX / C 标准库
  ### 进程 / 文件 / 标准 I/O / 内存 / 字符串 / 时间 / 错误 / 目录 / IPC / 网络
  （每表：函数 | 头文件 | 注意）

## 第 2 部分 · ToyOS 特色 API
  ### GUI / 图形 / 网络便利 / 文件系统 / 系统调用 / 版本宏

## 附录 · 已知缺口 / 内核服务（非用户 API）
```

**目录 / 网络在两轨都要出现时**：

- 第 1 轨写 POSIX 名（或「尚未提供 / 见第 2 轨」）。  
- 第 2 轨写 `OpenDirectory` / `ToyNetConnectIn` / 主机序 `connect`（按你确认的命名决议）。



### 5.3 落地顺序（确认后）


| 步     | 内容                                                     | 可测                     |
| ----- | ------------------------------------------------------ | ---------------------- |
| **A** | 只改 `API速查.md` 双轨排版 + 注意列 + 商店文案                        | 文档审阅                   |
| **B** | `dirent.h` 别名 + CRT `readdir` 包装；`DIRDEMO` 仍用旧 API 应仍过 | `./build.sh` + DirDemo |
| **C** | 按 §7 决议改网络头/实现；更新 NetLibDemo                           | `./build.sh` + NETLIB  |


一步一刀；不改 syscall 号；新 `.c` ≤300 行。

---



## 6. 打算如何处理目录 API（部分 B）

**保留第 2 轨（不变）**：

```c
TOY_DIR *OpenDirectory(...);
int ReadDirectory(TOY_DIR *, TOY_DIR_ENT *);
int CloseDirectory(TOY_DIR *);
int FileStat(...);   /* 不叫 stat，避免与未来 POSIX stat 抢名 */
```

**第 1 轨别名（拟）**：

```c
typedef TOY_DIR DIR;
/* 注意：不能简单 typedef TOY_DIR_ENT 为 struct dirent —— 布局/用法不同 */
struct dirent {
    char d_name[TOY_ENT_NAME_MAX];
    /* 可选：映射 Attr/Size 的教学字段，速查写明非完整 POSIX */
};

#define opendir(p)  OpenDirectory(p)
#define closedir(d) CloseDirectory(d)

struct dirent *readdir(DIR *dir);  /* CRT 新函数，非宏 */
```

`readdir` **实现要点**（`User/crt/dirent.c` 或拆 `dirent_posix.c`）：

1. 每 `DIR` 需挂钩「上次返回的 `struct dirent`」——**不能**用进程级单一静态缓冲（多目录并发会坏）；挂在 `TOY_DIR` 扩展字段更干净。
2. 内部调 `ReadDirectory`；返回 0 → `readdir` 返回 NULL；失败设 `errno`。
3. **扩展** `struct ToyDirectory` 只改 CRT 私有布局，不改 syscall。
4. `FileStat` ↔ POSIX `stat`：**本刀不做**（缺口另列；避免与 `TOY_FILE_STAT` 抢名）。

**兼容性**：`DirDemo` / `FsUtilListDir` 继续只调 `OpenDirectory`，零改动可通过。

---



## 7. 打算如何处理网络 API（部分 C）— 须你拍板



### 7.1 冲突

```c
/* 现有（NetLibDemo / ToyNet.c） */
int connect(int fd, unsigned ip, unsigned port);

/* 任务书 POSIX */
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
```

二者不能并存于同一链接单元。

### 7.2 方案（请选）


| 方案               | 做法                                                                                                                        | 优点                        | 代价                                                             |
| ---------------- | ------------------------------------------------------------------------------------------------------------------------- | ------------------------- | -------------------------------------------------------------- |
| **C1（推荐，贴近任务书）** | 现有 `(fd,ip,port)` **改名为** `ToyNetConnect` / `ToyNetBind`；`connect`/`bind` 让给 POSIX `sockaddr`（网络序）；内部转主机序再调 `toy_connect` | 第 1 轨名真正 POSIX；第 2 轨名符合规范 | **改现有符号名**；须改 `NetLibDemo`/`Pkg/Net` + 升 `TOY_NET_ABI_VERSION` |
| **C2（保守）**       | 保持现有 `connect(fd,ip,port)`；POSIX 版用新名或仅文档写「无完整 POSIX connect」                                                             | 零破坏 demos                 | **达不到**「POSIX 名 connect」课标                                     |
| **C3**           | `sys/socket.h` 与 `ToyNet.h` 用宏抢 `connect`                                                                                 | 易踩坑、课难讲                   | **不推荐**                                                        |


**建议默认走 C1**，并在部分 A 速查里写：

- 第 1 轨：`connect`/`bind` + `sockaddr_in`（网络序；注意列写清）  
- 第 2 轨：`ToyNetConnect`/`ToyNetBind`/`ToyNetIpv4`/`ToyNetResolve`/`ToyNetConnectIn`（主机序）

`socket`/`listen`/`accept`/`send`/`recv`：名称可留在第 1 轨；`accept` 仍无对端地址出参 — 注意列写明。

**版本**：C1 属破坏性 → `TOY_NET_ABI_VERSION_MAJOR` 递增（具体 2.0.0 或 1.3+迁移说明，由你定）。

---



## 8. 第 1 / 第 2 轨清单对照（实况 → 目标）



### 第 1 轨（已有 / 缺口）


| 章节     | 已有                                | 缺口或债                          |
| ------ | --------------------------------- | ----------------------------- |
| 进程     | fork wait execve exit kill signal | getpid；wait 退出码               |
| 文件     | open read write close lseek       | flags 忽略；无 truncate           |
| 标准 I/O | fopen fread fwrite fclose …       | 无 stdin FILE*；`w` 不截断         |
| 内存     | malloc… mmap munmap brk           |                               |
| 字符串    | strlen strcmp memcpy…             |                               |
| 时间     | sleep usleep msleep clock_ms      | 非墙钟                           |
| 错误     | errno 等                           |                               |
| 目录     | **无 POSIX 名**                     | 部分 B：opendir/readdir/closedir |
| IPC    | pipe dup                          | dup2？                         |
| 网络     | 名有形参非 POSIX                       | 部分 C：按 §7                     |




### 第 2 轨（已有）


| 模块      | API                                                                                   |
| ------- | ------------------------------------------------------------------------------------- |
| GUI     | `ToyUi*`（另：`unistd` 旧 `create_window` 等 — 速查可标「遗留，课用 ToyUi」）                          |
| 图形      | `ToyGfx*`                                                                             |
| 网络      | `ToyNetIpv4` `ToyNetResolve` `ToySockAddrIn` `ToyNet*In`；（C1 后）`ToyNetConnect`/`Bind` |
| FS      | `FsUtil*`；`OpenDirectory`/`ReadDirectory`/`CloseDirectory`/`FileStat`                 |
| syscall | `toy_*` / `toy_yield`                                                                 |
| 版本      | `TOYOS_CRT_VERSION_*`；各 lib `TOY_*_ABI_VERSION_*` / `FS_UTIL_*`                       |


---



## 9. 不要做的事（执行期自检）

- ❌ 改 syscall 号  
- ❌ 静默改 demos 依赖的符号而不升 ABI / 不改速查  
- ❌ 一次做完 A+B+C  
- ❌ 新实现文件 >300 行  
- ❌ 用宏把 `connect` 在 ToyNet/socket 间来回拧（C3）

---



## 10. 请你确认的问题

1. **部分 A**：是否同意按 §5.2 改 `Documents/开发/API速查.md`（含商店文案改为 Worker/Enqueue）？
2. **部分 C**：选 **C1 / C2 / 其他**？
3. **部分 B**：`readdir` 的 `struct dirent` 是否只保证 `d_name[]`（教学最小集）？
4. **遗留 GUI**：`create_window` 等是否在第 2 轨标「遗留」，课路径只推 `ToyUi`*？

你回复确认（尤其是 **C1 vs C2**）后，再执行**仅部分 A**。