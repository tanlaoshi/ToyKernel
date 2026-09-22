# ToyOS API 速查

> 一页对照。完整说明见 [`应用开发指南.md`](应用开发指南.md)。以 `User/include/` 为准。

## 进程

| 函数 | 头文件 | 注意 |
|------|--------|------|
| `exit(int status)` | `<stdlib.h>` | |
| `fork()` | `<unistd.h>` | 返回值当 pid（槽位+1） |
| `wait(int *status)` | `<unistd.h>` | **无 pid 参数**；CRT 把 `*status` 写成 **0** |
| `execve(path, argv, envp)` | `<unistd.h>` | |
| `kill(pid, sig)` | `<signal.h>` | 仅 SIGINT / KILL / TERM |
| `signal(sig, handler)` | `<signal.h>` | 教学级；无 sigaction |
| `toy_yield()` | `<toyos/syscall.h>` | **没有** libc `yield()` |
| `sleep` / `usleep` / `msleep` | `<unistd.h>` | 按调度节拍阻塞（课堂 ≈ms；非墙钟） |
| `clock_ms()` | `<unistd.h>` | 与 sleep 同尺；稳节拍用 |

## 文件

| 函数 | 头文件 | 注意 |
|------|--------|------|
| `open(path, flags)` | `<fcntl.h>` | 内核**忽略** flags |
| `read` / `write` / `close` | `<unistd.h>` | |
| `lseek(fd, off, whence)` | `<unistd.h>` | `SEEK_SET/CUR/END`；管道/套接字 `ESPIPE` |
| `fopen` / `fread` / `fwrite` / `fseek` / `ftell` / `fclose` | `<stdio.h>` | 无缓冲；**无 stdin FILE\***；`fopen("w")` **不截断** |
| `FileStat(path, st)` | `<dirent.h>` | |
| `OpenDirectory(path)` | `<dirent.h>` | 返回 **`TOY_DIR *`**，不是 int fd |
| `ReadDirectory` / `CloseDirectory` | `<dirent.h>` | |
| `FsUtilJoin` / `ToyosPath` / `ListDir` | `<FsUtil.h>` | 卷前缀 `TOYOS:` / `ESP:` / `RES:` |

## IPC / 内存

| 函数 | 头文件 |
|------|--------|
| `pipe(fds)` / `dup(fd)` | `<unistd.h>` |
| `malloc` / `calloc` / `realloc` / `free` | `<stdlib.h>` |
| `brk(addr)` | `<unistd.h>` |
| `mmap` / `munmap` | `<sys/mman.h>` |

## GUI（`libToyUi` 1.1.0 / `libToyGfx` 1.2.0）

| 函数 | 头文件 |
|------|--------|
| `ToyUiCreateWindow` / `SetLabel` / `AddButton`(id 0..3) / `Poll` | `<ToyUi.h>` |
| `ToyUiAddCheckBox` / `AddList` / `AddTextField` | `<ToyUi.h>` |
| `ToyGfxDamageText` / `DamageRect`（单次 ≤64×64） | `<ToyGfx.h>` |
| `ToyGfxDrawPixel` / `DrawLine` / `FillRect` / `DrawRect` | `<ToyGfx.h>` |

Poll：`0` 无 / `1` 关窗 / `100+id` 按钮 / `200+` 复选 / `220+` 列表 / `240+` 输入框 / **`300+HID` 键**（焦点在本窗）/ `400+` 客户区点。窗槽最多 6。libToyUi **1.2.0**。

## 网络（`libToyNet` 1.2.0；内核 `LWIP=1`；Guest `lwip on`）

| 函数 | 注意 |
|------|------|
| `socket` / `bind` / `listen` / `accept` | `<ToyNet.h>` |
| `connect(fd, ip, port)` | **主机序** `unsigned`，不是 POSIX `sockaddr` |
| `ToySockAddrIn` + `ToyNetConnectIn` / `BindIn` / `GetAddrIn` | 同样主机序 |
| `send` / `recv` | socket fd 上即 `write` / `read` |
| `ToyNetIpv4(a,b,c,d)` | **函数** |
| `ToyNetResolve(name, &ip)` | 点分 / `localhost` / 域名 |

## 常用 C

`printf` / `snprintf` — `<stdio.h>`。`strlen` / `strcmp` / `strstr` / `strchr` / `memcpy` — `<string.h>`。`atoi` / `qsort` — `<stdlib.h>`。

## 已知缺口

`getpid` / libc `yield()` / `getcwd` / `chdir` / `wait` 真实退出码 / Gfx 位图 / 滚动条。

`fopen` / `lseek` / `realloc` / `sleep`/`msleep`/`clock_ms` / 点线矩形 / 复选框列表输入框 **已经有**，不要当成缺口。

## 商店（内核服务 · 非用户 API）

桌面 **Store UI** = 异步 `StoreJob`；Shell `store …` = **同步**且与 UI Job 互斥。详见 [`应用开发指南.md`](应用开发指南.md)「十四、商店分发」与路线图 [`PR-S-job`](../路线图.md#pr-s-job)。
