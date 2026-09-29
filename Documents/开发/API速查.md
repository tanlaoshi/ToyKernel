# ToyOS API 速查

> 一页对照，**双轨**排版。完整说明见 [`应用开发指南.md`](应用开发指南.md)；规格见 [`ABI-API双轨制.md`](ABI-API双轨制.md)。以 `User/include/` 为准。
>
> | 轨 | 含义 | 头文件习惯 |
> | -- | ---- | ---------- |
> | **第 1 轨** | POSIX / C 习惯名；语义尽量接近，差异写在「注意」 | `unistd.h` `stdio.h` `fcntl.h` …；网络 POSIX 目标头 `sys/socket.h` |
> | **第 2 轨** | Toy 命名规范 | `ToyUi.h` `ToyGfx.h` `ToyNet.h` `FsUtil.h` `dirent.h` `toyos/` |
>
> **Syscall 号**：权威 [`SyscallABI.h`](../../Include/SyscallABI.h)（段内双轨）。用户 `toyos/syscall.h` **包含**该头。  
> **开课版本**：[`开课ABI冻结.md`](开课ABI冻结.md)。自学入口：[`开发者接手指南.md`](开发者接手指南.md)。

---

# 第 1 部分 · POSIX / C 标准库

## 进程

| 函数 | 头文件 | 注意 |
|------|--------|------|
| `exit(int status)` | `<stdlib.h>` | → `SYS_EXIT`（50） |
| `fork()` | `<unistd.h>` | 返回值当 pid（槽位+1）；`SYS_FORK`（51） |
| `wait(int *status)` | `<unistd.h>` | **无 pid**；`*status=(exit&0xff)<<8`；`WEXITSTATUS`；裸 syscall 退出码在 `rdx`；`SYS_WAIT`（52） |
| `execve(path, argv, envp)` | `<unistd.h>` | `SYS_EXECVE`（53） |
| `getpid` / `getppid` | `<unistd.h>` | `SYS_GETPID`（54）/ `SYS_GETPPID`（55）；无父时 ppid=0 |
| `gettid` / `toy_gettid` | `<toyos/thread.h>` | `SYS_GETTID`（56）；→ `TASK.Id` |
| `toy_thread_create` / `join` / `exit` | `<toyos/thread.h>` | `SYS_THREAD_*`（0/1/2）；经 `ToyThreadRoot`；多线程 `fork`→`-EAGAIN` |
| `pthread_create` / `join` / `exit` | `<pthread.h>` | thr-4 薄封装；非 Linux ABI |
| `pthread_mutex_*` | `<pthread.h>` | 自旋教学版（`sched_yield` 忙等） |
| `THREADDEMO.ELF` | Apps | thr-5 课堂 demo：`exec THREADDEMO.ELF` → `threaddemo: ok` |
| `ToyThreadRoot` / `toy_tls_tid` | `<toyos/thread.h>` | 入口桩 + TLS tid（`%fs:0` / TP） |
| `kill(pid, sig)` | `<signal.h>` | 仅 SIGINT / KILL / TERM；`SYS_KILL`（151） |
| `signal(sig, handler)` | `<signal.h>` | 教学级；无 `sigaction`；`SYS_SIGNAL`（152） |
| `sched_yield()` / `toy_yield()` | `<sched.h>` / 宏别名 | `SYS_YIELD`（150） |
| `sleep` / `usleep` / `msleep` | `<unistd.h>` | 按调度节拍阻塞（课堂 ≈ms；**非墙钟**）；`SYS_SLEEP`（750） |
| `clock_ms()` | `<unistd.h>` | 与 sleep 同尺；`SYS_CLOCK_MS`（700） |

## 文件

| 函数 | 头文件 | 注意 |
|------|--------|------|
| `open(path, flags)` | `<fcntl.h>` | 内核**忽略** flags；`SYS_OPEN`（350） |
| `read` / `write` / `close` | `<unistd.h>` | 351 / 352 / 353；read EOF=`0`；**write=0 视为错误** |
| `lseek(fd, off, whence)` | `<unistd.h>` | `SEEK_SET/CUR/END`；管道/套接字 `ESPIPE`；354 |
| `truncate` / `ftruncate` | — | **尚未提供** |

## 标准 I/O

| 函数 | 头文件 | 注意 |
|------|--------|------|
| `fopen` / `fread` / `fwrite` / `fseek` / `ftell` / `fclose` | `<stdio.h>` | 无缓冲；**无 stdin `FILE*`**；`fopen("w")` **不截断**（先 `remove`） |
| `printf` / `snprintf` | `<stdio.h>` | 格式子集；优先看串口 |

## 内存 / IPC

| 函数 | 头文件 | 注意 |
|------|--------|------|
| `malloc` / `calloc` / `realloc` / `free` | `<stdlib.h>` | |
| `brk(addr)` | `<unistd.h>` | `SYS_BRK`（250） |
| `mmap` / `munmap` | `<sys/mman.h>` | 251 / 252 |
| `pipe(fds)` / `dup(fd)` | `<unistd.h>` | 650 / 651；`dup2`：**尚未提供**（缺口） |

## 字符串 / 其它 C

| 函数 | 头文件 | 注意 |
|------|--------|------|
| `strlen` / `strcmp` / `strstr` / `strchr` / `memcpy` … | `<string.h>` | |
| `atoi` / `qsort` | `<stdlib.h>` | |
| `errno` 等 | `<errno.h>` | 常用：`ENOENT=2` `EBADF=9` `ENOMEM=12` `EINVAL=22` **`ENOSYS=38`** |

## 目录（POSIX 名）

| 函数 | 头文件 | 注意 |
|------|--------|------|
| `opendir` / `closedir` | `<dirent.h>` | 宏 → `OpenDirectory` / `CloseDirectory`（syscall 500） |
| `readdir(DIR *)` | `<dirent.h>` | 返回内部 `struct dirent *`（仅 `d_name[]`）；结束 / 失败均 `NULL`（失败设 `errno`）；读项 syscall 501 |
| `getcwd` / `chdir` | `<unistd.h>` | 550 / 551；任务内相对路径；根为 `"/"`；`chdir` 目标须是目录 |
| `stat` / `fstat` | `<sys/stat.h>` | `stat`→`FileStat`/400；`fstat`→451；`st_mode` 仅 `S_IFDIR`/`S_IFREG` |

## 网络（POSIX 形参 · 第 1 轨）

| 函数 | 头文件 | 注意 |
|------|--------|------|
| `socket` / `listen` / `accept` / `send` / `recv` | `<sys/socket.h>` | 实现在 `libToyNet`；底层 850–854；`accept` **无**对端地址出参 |
| `connect` / `bind`（`sockaddr`） | `<sys/socket.h>` | **网络序**；CRT 转主机序后调 `ToyNetConnect`/`ToyNetBind` |
| `htons` / `ntohs` / `htonl` / `ntohl` | `<sys/socket.h>` | |

---

# 第 2 部分 · ToyOS 特色 API

## GUI（`libToyUi` **1.2.0**）

| 函数 | 头文件 | 注意 |
|------|--------|------|
| `ToyUiCreateWindow` / `SetLabel` / `AddButton`(id 0..3) / `Poll` | `<ToyUi.h>` | Poll 见表；窗槽最多 6 |
| `ToyUiAddCheckBox` / `AddList` / `AddTextField` | `<ToyUi.h>` | |
| `create_window` / `damage` / `poll_input` / `ui_button` | `<unistd.h>` | **遗留**裸名；**课用 `ToyUi*`** |

| `ToyUiPoll` | 含义 |
|-------------|------|
| `0` | 无事件 |
| `1` | 关窗 |
| `2` | 客户区点击未命中控件 |
| `100+id` | 按钮 |
| `200+id` / `220+id` / `240+id` | 复选 / 列表 / 输入焦点 |
| `300+hid` | 按键（Enter `0x28` Esc `0x29` BS `0x2A` 空格 `0x2C` 方向 `0x4F`–`0x52`） |
| `-1` | 无效 wid |

## 图形（`libToyGfx` **1.3.0**）

| 函数 | 头文件 | 注意 |
|------|--------|------|
| `ToyGfxInitialize` / `ToyGfxSystemFontRoot` | `<ToyGfx.h>` | 系统字根 `Assets/Fonts`；Initialize 恒 0 |
| `ToyGfxDamageText` / `DamageRect` | `<ToyGfx.h>` | 文字**不**写字体路径；单次 `DamageRect` ≤64×64 |
| `ToyGfxDrawPixel` / `DrawLine` / `FillRect` / `DrawRect` | `<ToyGfx.h>` | |
| 位图 / 滚动条 / 私有 `.fnt` | — | **缺口**（私有字另刀） |

## 网络便利（`libToyNet` **2.0.1**；内核 `LWIP=1`；Guest `lwip on`）

| 函数 | 头文件 | 注意 |
|------|--------|------|
| `ToyNetConnect(fd, ip, port)` / `ToyNetBind` | `<ToyNet.h>` | **主机序**；ABI 2.0 起由旧 `connect`/`bind`(ip,port) 改名 |
| `ToySockAddrIn` + `ToyNetConnectIn` / `BindIn` / `GetAddrIn` | `<ToyNet.h>` | 同样主机序 |
| `ToyNetIpv4(a,b,c,d)` | `<ToyNet.h>` | **函数**（非宏） |
| `ToyNetResolve(name, &ip)` | `<ToyNet.h>` | 点分 / `localhost` / 域名 |

## 文件系统 / 目录（Toy 名）

| 函数 | 头文件 | 注意 |
|------|--------|------|
| `OpenDirectory(path)` | `<dirent.h>` | 返回 **`TOY_DIR *`**，不是 int fd |
| `ReadDirectory` / `CloseDirectory` | `<dirent.h>` | 写入调用方 `TOY_DIR_ENT *`；返回 1=有 / 0=结束 / -1=失败（≠ POSIX `readdir`） |
| `FileStat(path, st)` | `<dirent.h>` | 第 2 轨；POSIX 名见上表 `stat`/`fstat` |
| `FsUtilJoin` / `ToyosPath` / `ListDir` | `<FsUtil.h>` | 卷前缀 `TOYOS:` / `ESP:` / `RES:` |

## 系统信息（任务快照）

| 函数 | 头文件 | 注意 |
|------|--------|------|
| `toy_task_snap(TOY_TASK_SNAP *)` | `<toyos/task.h>` | `SYS_TASK_SNAP`（**1200**）；与 Shell `ps` 同源；`Pid`=槽位+1；范例 `TASKMGR.ELF` |

## 系统调用 / 版本

| 符号 | 头文件 | 注意 |
|------|--------|------|
| `toy_syscall(n,…)` / `toy_syscall2` | `<toyos/syscall.h>` | 未知 `n` → **-ENOSYS(-38)**；`wait` 用 `toy_syscall2` 取 rdx |
| `TOYOS_CRT_VERSION_*` | `<toyos/version.h>` | **1.4.0** |
| 各 `*_ABI_VERSION_*` | `Toy*.h` / `FsUtil.h` | 见 [`开课ABI冻结.md`](开课ABI冻结.md) |
---

# 附录

## 已知缺口

排队中：无本刀 POSIX 缺口。Gfx **位图**仍缺。

下列**已经有**，不要当成缺口：`fopen` / `lseek` / `realloc` / `sleep`/`msleep`/`clock_ms` / 点线矩形 / 复选框列表输入框 / `ToyNetConnect`（2.x）/ `opendir`/`readdir` / POSIX `connect`/`bind`+`sockaddr` / `getcwd`/`chdir` / `WEXITSTATUS` / `sched_yield` / `getpid`/`getppid` / `stat`/`fstat` / **`-ENOSYS`** / Syscall 段内双轨号。

## 商店（内核服务 · 非用户 API）

桌面 **Store UI** 与 Shell `store …` 均为 **Enqueue → Worker**（INTERFACE 只入队，不在 Shell/窗上下文做装卸）。Shell 立即回提示符；用 `store job` / `store status` 查态。详见 [`应用开发指南.md`](应用开发指南.md)「十四、商店分发」与路线图 [`PR-S-job`](../路线图.md#pr-s-job) / [`PR-K-rm-exc`](../路线图.md#pr-k-rm-exc)。
