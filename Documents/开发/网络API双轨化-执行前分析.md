# 网络 API 双轨化 · 执行前分析（C1 已定）

> **决策已定**：`connect`/`bind` 归 POSIX；主机序便利版改名 `ToyNetConnect`/`ToyNetBind`。  
> **本文**：对照仓库实况校正任务书 4 步；**确认后再做第 1 步**（只改名 + 迁移，不加 POSIX 原型）。

---

## 0. 结论摘要

| # | 结论 |
| - | ---- |
| 1 | **无** `sys/socket.h`；`AF_INET`/`SOCK_STREAM`/`connect`/`bind` 全在 `ToyNet.h` + `ToyNet.c`。 |
| 2 | C 调用点仅 **`NetLibDemo.c`**、**`User/Pkg/Net/main.c`**（及 SDK Examples/Net 若同源）。`NetDemo.S` / `NetServer.S` 走**裸 syscall**（主机序），**不必**改汇编即可过第 1 步。 |
| 3 | `ToyNetConnectIn`/`BindIn` 今日调用 `connect`/`bind`；第 1 步后必须改为调 `ToyNetConnect`/`ToyNetBind`。 |
| 4 | 任务书第 4 步「创建 `dirent.c`」**错误**：`User/crt/dirent.c` **已存在**（OpenDirectory 实现）。`readdir` 样例也有逻辑/类型错误（见 §8）。 |
| 5 | 第 1 步可独立：`TOY_NET_ABI` 升 **MAJOR=2**；编过 + `netlibdemo` 可测。第 2–3 步再建 `sys/socket.h` + `crt/socket.c`。 |
| 6 | `sys/types.h` **尚无** `sa_family_t` / `socklen_t` / `uint16_t`；第 2 步须补（或 `#include <stdint.h>`，若 CRT 无则加最小 typedef）。 |

---

## 1. `ToyNet.h` 现有结构

路径：`User/include/ToyNet.h`（ABI **1.2.0**）。

| 内容 | 现状 |
| ---- | ---- |
| 常量 | `AF_INET` `SOCK_STREAM` `INADDR_ANY`；`TOY_NET_SOCK_RESOLVE` |
| 主机序结构 | `ToySockAddrIn { Family, Port, Addr }`（**非**网络序） |
| 便利 | `ToyNetIpv4`；**`connect(fd,ip,port)` / `bind(fd,ip,port)`** ← 将改名 |
| 结构体 API | `ToyNetConnectIn` / `ToyNetBindIn` / `ToyNetAddrIn` / `ToyNetGetAddrIn` |
| 其它 | `socket` `listen` `accept(fd)` `send` `recv` `ToyNetResolve` |

第 1 步后头文件应变为：

- **删除** `int connect(int, unsigned, unsigned)` / `int bind(...)`  
- **新增** `ToyNetConnect` / `ToyNetBind`（`port` 用 `unsigned short` 与任务书一致；现实现是 `unsigned port`，改名时可收窄）  
- `socket`/`listen`/`accept`/`send`/`recv`：**第 1 步仍留在 ToyNet**（避免半截链接）；第 2–3 步再在 `sys/socket.h` 声明，实现可仍链自 libToyNet 或迁入 `socket.c`（另刀定）

---

## 2. 是否有 `sys/socket.h`

**没有。** 仅有 `sys/types.h`、`sys/mman.h`。

---

## 3. `ToyNet.c` 现有实现

路径：`User/Library/ToyNet/ToyNet.c`（约 216 行，&lt;300）。

| 函数 | 行为 |
| ---- | ---- |
| `connect`/`bind` | 调 `toy_connect` / `toy_bind`（主机序）；失败走 `ToyNetFail` |
| `ToyNetConnectIn`/`BindIn` | 校验 `Family==AF_INET` 后 **直接调 `connect`/`bind`** |
| `socket`/`listen`/`accept`/`send`/`recv` | 现有；`send`/`recv` = `write`/`read` |

第 1 步改动面小：改名两函数 + 改 In 包装的调用目标；**不改 syscall 号**。

---

## 4. NETLIB / NETDEMO / NETSRV 实况

| 产物 | 源 | 与 `connect(fd,ip,port)` |
| ---- | -- | ------------------------ |
| **NETLIB.ELF** | `User/Apps/NetLibDemo.c` + `libToyNet.a` | **有**：`connect(Fd, ToyNetIpv4(10,0,2,2), 8888)` → 改为 `ToyNetConnect` |
| **NETDEMO.ELF** | `User/Apps/NetDemo.S` | **无 C 调用**；`syscall` 直传主机序 IP/port → **第 1 步可不改** |
| **NETSRV.ELF** | `User/Apps/NetServer.S` | **无 C 调用**；裸 `bind(fd, INADDR_ANY, 9000)` syscall → **可不改** |
| **Pkg/Net** | `User/Pkg/Net/main.c` | **有**：同 NetLibDemo → 改 `ToyNetConnect` |
| SDK Examples/Net | 若拷贝同逻辑 | 同步改（若在树内） |

任务书写「NETLIB.c」——仓库里是 **`NetLibDemo.c`**，无 `NETLIB.c` 文件名。

---

## 5. `dirent.h` 现有结构

见既有分析：仅 `OpenDirectory` / `ReadDirectory` / `CloseDirectory` / `FileStat`；**无** POSIX 别名。  
（第 4 步另做；**不要**和第 1 步绑死。）

---

## 6. `User/crt/` 现有文件

已有：`dirent.c` `errno.c` `malloc.c` `printf.c` `signal.c` `sleep.c` `stdio.c` `stdlib.c` `string.c` `unistd.c` + 各 arch `crt0`/`syscall`。

**没有** `socket.c` → 第 3 步新建，并挂进 Makefile `USER_CRT_OBJS`。

---

## 7. 打算如何组织 POSIX socket（第 2–3 步，本刀不做）

```
应用课写法:
  #include <sys/socket.h>     /* POSIX connect/bind + sockaddr */
  #include <ToyNet.h>         /* ToyNetConnect / ToyNetIpv4 / Resolve … */

sys/socket.h
  类型 sockaddr / sockaddr_in / in_addr
  声明 socket/bind/connect/listen/accept/send/recv
  htons/ntohs/htonl/ntohl（可放 socket.c 或 netinet）

User/crt/socket.c
  POSIX connect/bind：校验 AF_INET → ntohl/ntohs → ToyNetConnect/ToyNetBind
  字节序辅助
  （可选）accept：内核无对端地址时忽略 addr/addrlen 或填零并在速查写注意

ToyNet.h
  #include <sys/socket.h> 复用 AF_*（避免双份宏）
  或保留 AF_* 并文档「与 socket.h 同值」
```

**链接**：`netlibdemo` 已链 `USER_CRT_OBJS` + `libToyNet.a` → POSIX `connect` 进 CRT 后与 `ToyNetConnect` 同链无冲突。

**accept 注意**：现 `accept(int fd)`；POSIX 三参数版第 3 步可做成「忽略出参」薄包装，syscall 号不变。

**inet_aton / inet_ntoa**：可放第 3 步末或第 3b；注意 `inet_ntoa` 静态缓冲课上要讲。

---

## 8. 打算如何处理目录 API（第 4 步，本刀不做）

**校正任务书样例**：

| 任务书问题 | 应改为 |
| ---------- | ------ |
| `typedef TOY_DIR_ENT struct dirent;` | 非法；应 `struct dirent { char d_name[…]; … };`，由 `readdir` 填充 |
| 「创建 dirent.c」 | **扩展**已有 `User/crt/dirent.c`（或拆 `dirent_posix.c` ≤300） |
| `static TOY_DIR_ENT` + `ReadDirectory != 0` | `ReadDirectory`：**1=有项，0=结束，-1=失败**；且静态缓冲多目录不安全 → 缓冲挂在 `TOY_DIR` 内 |
| 返回 `&Entry` 类型为 `TOY_DIR_ENT*` | 应返回 `struct dirent*` |

`opendir`/`closedir` 宏别名到 `OpenDirectory`/`CloseDirectory` 可行。

---

## 9. 第 1 步执行清单（确认后只做这些）

1. `ToyNet.h`：删 `connect`/`bind` 声明；加 `ToyNetConnect`/`ToyNetBind`；注释与 ABI **2.0.0**；更新「主机序」说明。  
2. `ToyNet.c`：函数改名；`ToyNetConnectIn`/`BindIn` 改调新名。  
3. 迁移：`NetLibDemo.c`、`Pkg/Net/main.c`（+ 树内 SDK 例程若有）。  
4. **不**动 `NetDemo.S`/`NetServer.S`（除非想改注释）。  
5. **不**建 `sys/socket.h` / `socket.c`。  
6. `./build.sh`；冒烟：Guest `lwip on` + `exec NETLIB.ELF`（或文档记 QEMU 依赖）。  
7. 可选：`Documents/开发/API速查.md` 一行提示「主机序请用 ToyNetConnect」——若你要求可并入第 1 步，否则留部分 A。

---

## 10. 请确认

1. 第 1 步范围是否同意 §9（含 ABI → **2.0.0**）？  
2. `port` 形参用 `unsigned short`（任务书）还是保持 `unsigned`（现状）？  
3. 第 1 步是否顺手改 API 速查网络三行，还是严格只改代码？  

你确认后我只做**第 1 步**。

---

## 11. 详细测试步骤（网-1）

完整步骤与期望表见路线图 **[`PR-U-abi-dual`](../路线图.md#pr-u-abi-dual)**「详细测试（网-1 · 必做）」：T0 符号/编译、T1 Guest+`nc -l -p 8888`+`lwip on`+`exec NETLIB.ELF`、T2 NETDEMO 回归、T3 旧 API 应编不过。
