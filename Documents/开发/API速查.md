# ToyOS API 速查

> 一页对照，**双轨**排版。完整说明见 [`应用开发指南.md`](应用开发指南.md)；规格见 [`ABI-API双轨制.md`](ABI-API双轨制.md)。以 `User/include/` 为准。
>
> | 轨 | 含义 | 头文件习惯 |
> | -- | ---- | ---------- |
> | **第 1 轨** | POSIX / C 习惯名；语义尽量接近，差异写在「注意」 | `unistd.h` `stdio.h` `fcntl.h` …；网络 POSIX 目标头 `sys/socket.h`（刀 C） |
> | **第 2 轨** | Toy 命名规范 | `ToyUi.h` `ToyGfx.h` `ToyNet.h` `FsUtil.h` `dirent.h` `toyos/` |

---

# 第 1 部分 · POSIX / C 标准库

## 进程

| 函数 | 头文件 | 注意 |
|------|--------|------|
| `exit(int status)` | `<stdlib.h>` | |
| `fork()` | `<unistd.h>` | 返回值当 pid（槽位+1） |
| `wait(int *status)` | `<unistd.h>` | **无 pid 参数**；CRT 把 `*status` 写成 **0**（真实退出码：缺口） |
| `execve(path, argv, envp)` | `<unistd.h>` | |
| `kill(pid, sig)` | `<signal.h>` | 仅 SIGINT / KILL / TERM |
| `signal(sig, handler)` | `<signal.h>` | 教学级；无 `sigaction` |
| `sleep` / `usleep` / `msleep` | `<unistd.h>` | 按调度节拍阻塞（课堂 ≈ms；**非墙钟**） |
| `clock_ms()` | `<unistd.h>` | 与 sleep 同尺；稳节拍用 |
| `getpid` | — | **尚未提供**（缺口） |

## 文件

| 函数 | 头文件 | 注意 |
|------|--------|------|
| `open(path, flags)` | `<fcntl.h>` | 内核**忽略** flags |
| `read` / `write` / `close` | `<unistd.h>` | |
| `lseek(fd, off, whence)` | `<unistd.h>` | `SEEK_SET/CUR/END`；管道/套接字 `ESPIPE` |
| `truncate` / `ftruncate` | — | **尚未提供** |

## 标准 I/O

| 函数 | 头文件 | 注意 |
|------|--------|------|
| `fopen` / `fread` / `fwrite` / `fseek` / `ftell` / `fclose` | `<stdio.h>` | 无缓冲；**无 stdin `FILE*`**；`fopen("w")` **不截断** |
| `printf` / `snprintf` | `<stdio.h>` | |

## 内存 / IPC

| 函数 | 头文件 | 注意 |
|------|--------|------|
| `malloc` / `calloc` / `realloc` / `free` | `<stdlib.h>` | |
| `brk(addr)` | `<unistd.h>` | |
| `mmap` / `munmap` | `<sys/mman.h>` | |
| `pipe(fds)` / `dup(fd)` | `<unistd.h>` | `dup2`：**尚未提供**（缺口） |

## 字符串 / 其它 C

| 函数 | 头文件 | 注意 |
|------|--------|------|
| `strlen` / `strcmp` / `strstr` / `strchr` / `memcpy` … | `<string.h>` | |
| `atoi` / `qsort` | `<stdlib.h>` | |
| `errno` 等 | `<errno.h>` | |

## 目录（POSIX 名）

| 函数 | 头文件 | 注意 |
|------|--------|------|
| `opendir` / `closedir` | `<dirent.h>` | 宏 → `OpenDirectory` / `CloseDirectory` |
| `readdir(DIR *)` | `<dirent.h>` | 返回内部 `struct dirent *`（仅 `d_name[]`）；结束 / 失败均 `NULL`（失败设 `errno`） |
| `stat` / `getcwd` / `chdir` | — | **尚未提供**（缺口；勿与第 2 轨 `FileStat` 抢名） |

## 网络（POSIX 形参 · 目标）

| 函数 | 头文件 | 注意 |
|------|--------|------|
| `socket` / `listen` / `accept` / `send` / `recv` | `<ToyNet.h>`（暂） | 名可用；`accept` **无**对端地址出参；`send`/`recv` 即 `write`/`read` |
| `connect` / `bind`（`sockaddr`） | — | **尚未提供**；刀 **C** 进 `sys/socket.h`（网络序）。课上主机序请用第 2 轨 `ToyNetConnect` / `ToyNetBind` |
| `htons` / `htonl` … | — | 随刀 C |

---

# 第 2 部分 · ToyOS 特色 API

## GUI（`libToyUi`）

| 函数 | 头文件 | 注意 |
|------|--------|------|
| `ToyUiCreateWindow` / `SetLabel` / `AddButton`(id 0..3) / `Poll` | `<ToyUi.h>` | Poll：`0` 无 / `1` 关窗 / `100+id` 按钮 / `200+` 复选 / `220+` 列表 / `240+` 输入 / **`300+HID` 键** / `400+` 客户区点；窗槽最多 6 |
| `ToyUiAddCheckBox` / `AddList` / `AddTextField` | `<ToyUi.h>` | |
| `create_window` / `damage` / `poll_input` / `ui_button` | `<unistd.h>` | **遗留**裸 syscall 名；**课用 `ToyUi*`**，勿新写 |

## 图形（`libToyGfx`）

| 函数 | 头文件 | 注意 |
|------|--------|------|
| `ToyGfxDamageText` / `DamageRect` | `<ToyGfx.h>` | 单次 `DamageRect` ≤64×64 |
| `ToyGfxDrawPixel` / `DrawLine` / `FillRect` / `DrawRect` | `<ToyGfx.h>` | |
| 位图 / 滚动条 API | — | **缺口** |

## 网络便利（`libToyNet` **2.0.0**；内核 `LWIP=1`；Guest `lwip on`）

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
| `FileStat(path, st)` | `<dirent.h>` | **不叫** `stat` |
| `FsUtilJoin` / `ToyosPath` / `ListDir` | `<FsUtil.h>` | 卷前缀 `TOYOS:` / `ESP:` / `RES:` |

## 系统调用 / 版本

| 符号 | 头文件 | 注意 |
|------|--------|------|
| `toy_yield()` | `<toyos/syscall.h>` | **没有** libc `yield()` |
| 其它 `toy_*` | `<toyos/syscall.h>` | 内联包装；号见同头 |
| `TOYOS_CRT_VERSION_*` | `<toyos/version.h>` | |
| `TOY_NET_ABI_VERSION_*` 等 | 各 `Toy*.h` / `FsUtil.h` | 破坏性改名升 MAJOR |

---

# 附录

## 已知缺口

`getpid` / libc `yield()` / `getcwd` / `chdir` / `wait` 真实退出码 / POSIX `connect`/`bind`+`sockaddr`（刀 C）/ Gfx 位图 / 滚动条。

下列**已经有**，不要当成缺口：`fopen` / `lseek` / `realloc` / `sleep`/`msleep`/`clock_ms` / 点线矩形 / 复选框列表输入框 / `ToyNetConnect` / `opendir`/`readdir`/`closedir`。

## 商店（内核服务 · 非用户 API）

桌面 **Store UI** 与 Shell `store …` 均为 **Enqueue → Worker**（INTERFACE 只入队，不在 Shell/窗上下文做装卸）。Shell 立即回提示符；用 `store job` / `store status` 查态。详见 [`应用开发指南.md`](应用开发指南.md)「十四、商店分发」与路线图 [`PR-S-job`](../路线图.md#pr-s-job) / [`PR-K-rm-exc`](../路线图.md#pr-k-rm-exc)。
