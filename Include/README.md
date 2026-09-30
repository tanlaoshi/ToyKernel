# Include/ — 内核公开契约头（按域分子目录）

> **用户态头不在这里**，在 [`User/include/`](../User/include/)。应用 / CRT / SDK 只碰 `User/include/`；跨界契约仅 `Abi/SyscallABI.h`（由 `toyos/syscall.h` 与 `Tools/build-sdk.sh` 引入）。

## 分层

| 目录 | 谁用 | 内容 |
| ---- | ---- | ---- |
| `Abi/` | 内核 + 用户态（号表/errno/socket 常数） | `SyscallABI` `Errno` `Socket` `BootTypes` `ToyOsVersion` |
| `Hal/` | Common / Core 经门面访硬件 | `Hal*.h` |
| `Core/` | 内核核心 | 调度 / PMM / VMM / 进程 / Syscall / Device / BootInfo … |
| `Driver/` | 驱动框架 + Block 类 | `Driver*` `Block*` `HIDKeyboard` `PciNames` |
| `Library/` | 共享库公开面 | Elf Fat Gpt Bmp Font UI Vfs … |
| `Services/` | 系统服务 / GUI / Shell / Store / 网 | Gui Desktop Console Store LwIp … |

`*Private.h` 与对应公开头同目录，仅实现文件 include。

## 编译

`Makefile` 对上述子目录都加了 `-I`，源码继续写 `#include "Scheduler.h"`，不必写路径前缀。
