# ToyKernel 目录结构（文件级）

> **范围**：源码 / 头文件 / 脚本 / 文档 / 配置约 **1018** 项。
> **不含**：`Build/`、`ThirdParty/lwip/` 上游树、交叉工具链解压包、`.o/.a/.elf`、字体/图标/固件等二进制。
> **分层硬规则**：[`技术手册 · 体系结构`](../技术手册.md#i-体系结构--分层与对外接口)。**排期**：[`路线图.md`](../路线图.md)。
> **分层终态（上→下 = 依赖）**：`User → Services → Core → Library → HAL → Boot`。  
> **磁盘 · Code（仅此钉死）**：`CodeA-HAL` → `CodeB-Library`（含 Fonts/）→ `CodeC-Core`/`CodeC-Modules` → `CodeD-Services` → `CodeE-User`。  
> **磁盘 · Image**：`Assets/`、仓顶 `Store/`（货架）→ **`ToyImage/`**，不进 ToyKernel 顶层、不加 Code 前缀。  
> **日期**：2026-10-03（Code 预览；**Assets/Store 已迁 ToyImage**）。

## 目录

按**开机 Code A→E**列源码；契约 / 工具另组；**资源与货架见 ToyImage**。

**Code（钉死）**

- [`CodeA-HAL`](#8-HALBoard)
- [`CodeB-Library`](#5-CommonLibrary)（含 `Fonts/`，非层）
- [`CodeC-Core`](#3-Core) · [`CodeC-Modules`](#4-CommonModules)
- [`CodeD-Services`](#7-CommonServices)（含商店**逻辑** `Store/` 源码）
- [`CodeE-User`](#14-User)
- Boot：仓外 ToyBoot；仓内 Startup ∈ CodeA-HAL

**跨层契约 / 工具 / 产物**（无 Code 前缀）

- [`Include/`](#2-Include)
- [仓库根](#1-Root) · [`Tools/`](#16-Tools) · [`ThirdParty/`](#17-ThirdParty) · `Build/`
- [`Documents/`](#18-Documents) · [`Meta/`](#19-Meta)

**Image（ToyImage，非本仓顶层终态）**

- `ToyImage/Assets/` — Guest 资源种子  
- `ToyImage/Store/` — 商店目录与 packages（货架数据）  
- Kernel 顶层已无 `Assets/` / 仓顶 `Store/`  

[总览与依赖](#0-总览与模块依赖) · [附 B · ToyBoot / ToyImage](#附-b-与-toyboot--toyimage-边界)

## 0. 总览与模块依赖

**依赖（上→下）**：`User → Services → Core → Library → HAL → Boot`。  
**开机 / 磁盘（A→E）**：`CodeA-HAL → CodeB-Library → CodeC-Core → CodeD-Services → CodeE-User`（`ls` 即此序）。  
两套顺序**方向相反**：依赖图从上往下看；开机从 A 走到 E。

### 0.1 分层与依赖（钉死读法）

**两件事不要混**：

- **依赖层叠** — 下图。越靠上越靠近用户。  
- **开机 Code** — 字母 A 最早。磁盘目录用 `CodeA-…` 前缀，不靠手工编号。

```
依赖上↓                    磁盘 / 开机 →
┌──────────── User ─────┐   CodeE-User
│      Services         │   CodeD-Services
│      Core (+Modules)  │   CodeC-Core / CodeC-Modules
│      Library (+Fonts) │   CodeB-Library（Fonts/ 子目录）
│      HAL              │   CodeA-HAL          ← 进内核最先
│      Boot             │   （仓外 ToyBoot，早于 A）
└───────────────────────┘
```

依赖（只允许向下）：
  User ──syscall──► Core / Services
  Services ───────► Core / Library / Hal*
  Core ───────────► Library / Hal* / BootInfo
  Library ────────► Hal*
  HAL ────────────► 本 Arch Drivers（私有）
  Boot ──BOOT_INFO─► Core（开机交棒一次）

> 现源码仍为 `HAL/` `Library/` `Services/` `User/` 等短名；`Code*` 改名 = [TREE-rest](../路线图.md#pr-tree-rest)。

### 0.2 依赖矩阵

| 模块 | 可依赖 | 禁止依赖 |
| ---- | ------ | -------- |
| Include | （无） | 任何 `.c` |
| Core | Include；Modules；HAL Arch/Page/Timer | Drivers 私头；User |
| Modules | Include；Library；Hal* | Drivers 寄存器头；Services GUI |
| Library | Include；Hal*（块/网门面） | XHCI/E1000 等设备私头 |
| Services | Include；Library；Hal*；Modules API | `HAL/**/Drivers/**` 私头 |
| HAL Drivers | Driver.h；本设备私头；Hal Io/Pci | Services；User |
| User | `User/include`；SyscallABI | `Hal.h`；任何内核内部头 |

### 0.3 顶层树

**终态**（TREE-rest 后；Code A→E 钉死；种子在 ToyImage）：

```
ToyKernel/
├── CodeA-HAL/
├── CodeB-Library/       # 含 Fonts/
├── CodeC-Core/
├── CodeC-Modules/
├── CodeD-Services/      # 含 Store/ 逻辑 .c（非货架）
├── CodeE-User/
├── Include/
├── Tools/  Documents/  ThirdParty/
├── Makefile  build.sh  README.md
└── Build/
```

> Guest 资源 / 商店货架：`../ToyImage/Assets/`、`../ToyImage/…/Store/`（Image 阶段）。

**现树**（短名中转；Code 改名待 TREE-rest）：

```
ToyKernel/
├── HAL/                 # → CodeA-HAL
├── Library/             # → CodeB-Library（已抬顶）
├── Services/            # → CodeD-Services（已抬顶）
├── Core/                # → CodeC-Core
├── User/                # → CodeE-User
├── Common/              # ★ 将删
│   ├── Fonts/           # → CodeB-Library/Fonts/
│   └── Modules/         # → CodeC-Modules
├── Include/
├── Assets/  Tools/  Documents/  ThirdParty/
├── Makefile  build.sh  README.md
└── Build/
```

## 1. Root — 仓库根：构建入口与顶层配置

| 文件 | 用途 |
| ---- | ---- |
| `.gitignore` | 忽略 Build/、产物、旧 VirtRootFs 等 |
| `Makefile` | 多 ARCH 构建；编入 Drivers/Services 子目录 wildcard |
| `README.md` | 仓简介、最短上手、文档入口 |
| `build.sh` | 一键编译并同步 Kernel/ELF/Assets → ToyImage/RootFs |

## 2. `Include/` — 跨层公开 API / 契约头（按域分子目录）

> **用户态头**在 `User/include/`，不进本树。跨界契约仅 `Abi/`（`SyscallABI` / `Errno` / `Socket` …）。
> 说明见 [`Include/README.md`](../../Include/README.md)。Makefile 对子目录均 `-I`，源码仍 `#include "Xxx.h"`。

### Abi/ — 内核↔用户态共享契约

| 文件 | 用途 |
| ---- | ---- |
| `Include/Abi/BootTypes.h` | 架构无关基础类型（无 UEFI） |
| `Include/Abi/Errno.h` | 错误码数值（与 `User/include/errno.h` 对齐） |
| `Include/Abi/Socket.h` | socket 常数（简化 ABI） |
| `Include/Abi/SyscallABI.h` | 系统调用号段权威（SDK 打包进 ToySDK） |
| `Include/Abi/ToyOsVersion.h` | 版本字符串 |

### Hal/ — 硬件门面

| 文件 | 用途 |
| ---- | ---- |
| `Include/Hal/Hal.h` | HAL 统一接口 |
| `Include/Hal/HalConsole.h` | 控制台门面 |
| `Include/Hal/HalDevices.h` | 块 / USB 输入 / 网卡门面 |
| `Include/Hal/HalSerial.h` | 串口门面 |
| `Include/Hal/HalVideo.h` | 帧缓冲门面 |

### Core/ — 内核核心契约

| 文件 | 用途 |
| ---- | ---- |
| `Include/Core/BootInfo.h` | Startup → Common 启动快照 |
| `Include/Core/CoreOps.h` | Core 经 ops 调 Services |
| `Include/Core/Debug.h` | 内核调试 / 串口开关 |
| `Include/Core/Device.h` | 设备管理器 |
| `Include/Core/Kernel.h` | `KernelMain` |
| `Include/Core/KernelModules.h` | 子系统模块表 |
| `Include/Core/KernelModulesPrivate.h` | Modules / Init 内部 |
| `Include/Core/KernelTask.h` | 通用内核任务注册表 |
| `Include/Core/MemoryOps.h` | 可替换物理页政策 |
| `Include/Core/Module.h` | 可插拔模块框架 |
| `Include/Core/PhysicalMemory.h` | PMM 公开 API |
| `Include/Core/PhysicalMemoryPrivate.h` | 段表 / 框架内部 |
| `Include/Core/Process.h` | exec / 进程 |
| `Include/Core/ProcessPrivate.h` | 装载内部 |
| `Include/Core/Scheduler.h` | 调度器公开 API |
| `Include/Core/SchedulerOps.h` | 可替换调度政策 |
| `Include/Core/SchedulerPrivate.h` | 调度内部 |
| `Include/Core/SpinLock.h` | 自旋锁 |
| `Include/Core/Syscall.h` | 内核侧 syscall 分发 |
| `Include/Core/SyscallPrivate.h` | Syscall 内部 |
| `Include/Core/TaskFd.h` | 任务 FD 表 |
| `Include/Core/TaskFdPrivate.h` | FD 内部 |
| `Include/Core/ToySerialConfig.h` | 串口配置 |
| `Include/Core/ToySerialLog.h` | 串口日志宏 |
| `Include/Core/VirtualMemory.h` | VMM |

### Driver/ — 驱动框架与 Block

| 文件 | 用途 |
| ---- | ---- |
| `Include/Driver/Block.h` | 块设备抽象 |
| `Include/Driver/BlockMux.h` | Primary + MSC 双后端 |
| `Include/Driver/Driver.h` | 驱动注册表 |
| `Include/Driver/DriverBlock.h` | Block 类适配 |
| `Include/Driver/DriverInput.h` | Input 类适配 |
| `Include/Driver/DriverNet.h` | Net 类适配 |
| `Include/Driver/DriverNic.h` | 以太网 L2 |
| `Include/Driver/HIDKeyboard.h` | HID 键码 |
| `Include/Driver/PciNames.h` | PCI 可读名 |

### Library/ — 共享库公开面

| 文件 | 用途 |
| ---- | ---- |
| `Include/Library/Bmp.h` | BMP 解码 |
| `Include/Library/Elf.h` | ELF 装载 |
| `Include/Library/ElfPrivate.h` | Elf 内部 |
| `Include/Library/Fat.h` | FAT 接口 |
| `Include/Library/FatPrivate.h` | FAT 内部 |
| `Include/Library/Font.h` | 点阵字体 |
| `Include/Library/Gpt.h` | GPT |
| `Include/Library/GptPrivate.h` | GPT 内部 |
| `Include/Library/LibWrite.h` | Library 文本输出槽 |
| `Include/Library/UI.h` | 几何绘制 |
| `Include/Library/UiAction.h` | 按钮事件分发 |
| `Include/Library/UiButton.h` | 按钮 widget |
| `Include/Library/Vfs.h` | VFS / FsOps |

### Services/ — 系统服务 / GUI / Shell / 网

| 文件 | 用途 |
| ---- | ---- |
| `Include/Services/Console.h` | Shell |
| `Include/Services/ConsolePrivate.h` | Console 内部 |
| `Include/Services/Db.h` | KV 库 |
| `Include/Services/DbPrivate.h` | Db 内部 |
| `Include/Services/Desktop.h` | 桌面 |
| `Include/Services/DesktopPrivate.h` | Desktop 内部 |
| `Include/Services/DevicesUi.h` | 设备管理器 GUI |
| `Include/Services/DevicesUiPrivate.h` | DevicesUi 内部 |
| `Include/Services/EditUi.h` | 文本编辑器 |
| `Include/Services/EditUiPrivate.h` | EditUi 内部 |
| `Include/Services/FileSystem.h` | 文件系统服务 |
| `Include/Services/FileSystemPrivate.h` | FS 内部 |
| `Include/Services/FilesUi.h` | 文件浏览器 |
| `Include/Services/FilesUiPrivate.h` | FilesUi 内部 |
| `Include/Services/Gui.h` | 窗口管理 |
| `Include/Services/GuiPrivate.h` | Gui 内部 |
| `Include/Services/Install.h` | Guest 安装 |
| `Include/Services/Locale.h` | 本地化 |
| `Include/Services/LocalePrivate.h` | Locale 内部 |
| `Include/Services/LwIp.h` | lwIP 门面 |
| `Include/Services/NetConfig.h` | IPv4 / DNS 配置 |
| `Include/Services/SettingsUi.h` | Settings |
| `Include/Services/SettingsUiPrivate.h` | SettingsUi 内部 |
| `Include/Services/ShellCommands.h` | Shell 扩展命令 |
| `Include/Services/ShellPrivate.h` | Shell 内部 |
| `Include/Services/Store.h` | 本地商店 |
| `Include/Services/StoreJob.h` | Store 后台作业 |
| `Include/Services/StoreNetPrivate.h` | StoreNet 内部 |
| `Include/Services/StorePrivate.h` | Store 内部 |
| `Include/Services/StoreUi.h` | 商店 GUI |
| `Include/Services/StoreUiPrivate.h` | StoreUi 内部 |
| `Include/Services/Tasks.h` | 常驻任务 |
| `Include/Services/TasksPrivate.h` | Tasks 内部 |
| `Include/Services/Tcp.h` | 自研 TCP |
| `Include/Services/Theme.h` | 主题 |
| `Include/Services/ThemePrivate.h` | Theme 内部 |
| `Include/Services/TtyUi.h` | TTY UI |
| `Include/Services/TtyUiPrivate.h` | TtyUi 内部 |
| `Include/Services/Udp.h` | UDP |

## 3. `Core/` — 内核核心：启动编排、调度、进程、系统调用、虚拟内存

### Kernel

| 文件 | 用途 |
| ---- | ---- |
| `Core/Kernel/BootInfo.c` | 全局启动信息（由 HAL Startup 写入，Common 只读） |
| `Core/Kernel/CoreOps.c` | PR-R4：WindowOps / VfsServiceOps 注册与薄分发 |
| `Core/Kernel/Kernel.c` | 内核入口：早期 Video 设置、模块初始化、启动常驻任务 |
| `Core/Kernel/KernelModules.c` | 子系统模块表与 Run（PR-S3-kernelmodules-1） |
| `Core/Kernel/KernelModulesInit.c` | 子系统 Initialize*（PR-S3-kernelmodules-1） |
| `Core/Kernel/KernelTask.c` | 常驻内核任务（桌面 / Shell 等） |
| `Core/Kernel/Module.c` | 内核模块启动器 |

### Device

| 文件 | 用途 |
| ---- | ---- |
| `Core/Device/Device.c` | 平台无关设备表（PR-DEV-1） |
| `Core/Device/DeviceDump.c` | lsdev 可读行（PR-DEV-lsdev） |
| `Core/Device/DeviceResource.c` | PR-DEV-mmio-conflict：MMIO 区间登记与重叠检测。 |
| `Core/Device/DeviceTree.c` | PR-DEV-tree-api：设备父子拓扑 API（策略 A）。 |
| `Core/Device/DeviceTreeDump.c` | PR-DEV-tree-lsdev：lsdev -t 树状打印。 |

### PhysicalMemory

| 文件 | 用途 |
| ---- | ---- |
| `Core/PhysicalMemory/PhysicalMemory.c` | 物理页分配器入口。位图在段表里。 |
| `Core/PhysicalMemory/PhysicalMemoryAlloc.c` | 框架：锁、Lookup、公开 API 外壳、Total/FreeCount。 |
| `Core/PhysicalMemory/PhysicalMemoryOps.c` | 注册当前物理页分配政策。 |
| `Core/PhysicalMemory/PhysicalMemorySeg.c` | 分段位图。静态位图挂在内核所在段。 |

### Process

| 文件 | 用途 |
| ---- | ---- |
| `Core/Process/Process.c` | exec / execve / 嵌入演示（PR-S-process-1） |
| `Core/Process/ProcessAppFont.c` | PR-S-app-font：exec 时按 PKG font= 挂/卸应用私有字 |
| `Core/Process/ProcessLoad.c` | 读盘装载 ELF（PR-S-process-1） |
| `Core/Process/ProcessMem.c` | brk / mmap（PR-S-process-1） |

### Scheduler

| 文件 | 用途 |
| ---- | ---- |
| `Core/Scheduler/Scheduler.c` | 任务槽 / 当前任务 / 切换（PR-S3-sched-2） |
| `Core/Scheduler/SchedulerBoot.c` | idle、AP 与 SchedulerStart（PR-S-sched-1） |
| `Core/Scheduler/SchedulerCond.c` | PR-K-preempt-needresched：NeedResched + CondResched |
| `Core/Scheduler/SchedulerCreate.c` | 内核/用户任务创建（PR-S3-sched-2） |
| `Core/Scheduler/SchedulerFork.c` | fork COW 克隆（PR-S3-scheduser-1） |
| `Core/Scheduler/SchedulerIoBreath.c` | PR-K-preempt-breath：统一长 IO 呼吸 |
| `Core/Scheduler/SchedulerOps.c` | 注册当前调度政策。 |
| `Core/Scheduler/SchedulerPreempt.c` | PR-K-preempt-cs：嵌套 PreemptCount |
| `Core/Scheduler/SchedulerRunq.c` | 每核 READY 队列 / steal / PickNext（PR-S-sched-split-1） |
| `Core/Scheduler/SchedulerSignal.c` | 定时器与信号投递（PR-S-sched-1） |
| `Core/Scheduler/SchedulerSleep.c` | 用户态 sleep：原地 sti+hlt 等节拍（不切到 shell） |
| `Core/Scheduler/SchedulerTerminate.c` | PR-U-thread-3：收尸 / TerminateUserLocked / join 唤醒 |
| `Core/Scheduler/SchedulerThread.c` | PR-U-thread：CreateThread + 用户栈/TLS（thr-1/2） |
| `Core/Scheduler/SchedulerThreadSpin.c` | thr-1/2 调试：映 spin 入口 + CreateThread |
| `Core/Scheduler/SchedulerThreadSys.c` | PR-U-thread-3：THREAD_CREATE / JOIN / EXIT |
| `Core/Scheduler/SchedulerUser.c` | kill / signal / yield（PR-S3-scheduser-1） |
| `Core/Scheduler/SchedulerWait.c` | PR-S-sched-split-2：wait / orphan / exit 用户入口 |

### Syscall

| 文件 | 用途 |
| ---- | ---- |
| `Core/Syscall/Syscall.c` | 系统调用分发（入口无关） |
| `Core/Syscall/SyscallCwd.c` | getcwd / chdir，以及相对路径拼到任务 Cwd |
| `Core/Syscall/SyscallFs.c` | 文件与目录系统调用（PR-S-syscallfs-1） |
| `Core/Syscall/SyscallFsSocket.c` | socket 系统调用（PR-S-syscallfs-1） |
| `Core/Syscall/SyscallProc.c` | PR-S-syscall-split-1：execve / 用户窗 GUI 系统调用 |

### TaskFd

| 文件 | 用途 |
| ---- | ---- |
| `Core/TaskFd/TaskFd.c` | 槽位、打开与关闭（PR-S-taskfd-1） |
| `Core/TaskFd/TaskFdIo.c` | read / write / seek（PR-S-taskfd-1） |
| `Core/TaskFd/TaskFdPipe.c` | pipe / dup（PR-S-taskfd-1） |
| `Core/TaskFd/TaskFdSocket.c` | 套接字 fd（PR-S-taskfd-1） |

### VirtualMemory

| 文件 | 用途 |
| ---- | ---- |
| `Core/VirtualMemory/VirtualMemory.c` | 虚拟内存策略层（地址空间、用户区布局、VirtualMemoryCopyFromUser） |
| `Core/VirtualMemory/VirtualMemoryCow.c` | 写时复制：克隆与缺页（PR-S-virtualmemory-1） |

## 4. `Common/Modules/` — 可替换内核模块（SCHED / MEM / FS）

| 文件 | 用途 |
| ---- | ---- |
| `Common/Modules/FileSystemFat/Fat/FatDir.c` | 目录枚举、mkdir/rmdir（PR-S-fatdir-1） |
| `Common/Modules/FileSystemFat/Fat/FatDirName.c` | 目录名匹配与 8.3 别名（PR-S-fatdir-1） |
| `Common/Modules/FileSystemFat/Fat/FatDirScan.c` | 目录缓冲扫描与项读写（PR-S-fatdir-1） |
| `Common/Modules/FileSystemFat/Fat/FatDirSlot.c` | 空闲槽、扩目录、建项（PR-S-fatdir-1） |
| `Common/Modules/FileSystemFat/Fat/FatDirStress.c` | 大目录回归（PR-S-fatdir-1） |
| `Common/Modules/FileSystemFat/Fat/FatFile.c` | 整文件读、同步、IO 喘息（PR-S-fatfile-1） |
| `Common/Modules/FileSystemFat/Fat/FatFileDelete.c` | 删除与改名（PR-S-fatfile-1） |
| `Common/Modules/FileSystemFat/Fat/FatFileRead.c` | 偏移读（PR-S-fatfile-1） |
| `Common/Modules/FileSystemFat/Fat/FatFileWrite.c` | 整文件写编排（PR-F-fat-1） |
| `Common/Modules/FileSystemFat/Fat/FatFileWriteAt.c` | 偏移写（PR-S-fatfile-1） |
| `Common/Modules/FileSystemFat/Fat/FatFileWriteExist.c` | 旧簇链就地覆写（PR-F-fat-1） |
| `Common/Modules/FileSystemFat/Fat/FatFileWriteNew.c` | 分配新簇链并建/改目录项（PR-F-fat-1） |
| `Common/Modules/FileSystemFat/Fat/FatPath.c` | 路径解析（PR-S-fatpath-1） |
| `Common/Modules/FileSystemFat/Fat/FatPathName.c` | 8.3 / LFN 名称匹配（PR-S-fatpath-1） |
| `Common/Modules/FileSystemFat/FatFormat.c` | PR-FS-inst-1：简易 FAT32 格式化 |
| `Common/Modules/FileSystemFat/FatFsOps.c` | FAT 作为 VFS 第一后端（PR-F1 / PR-F2） |
| `Common/Modules/FileSystemFat/FatIo.c` | FAT 扇区/簇 I/O、FAT 链、卷几何初始化 |
| `Common/Modules/PhysicalMemoryBitmap/PhysicalMemoryBitmap.c` | first-fit 位图政策（PR-MEM-move）。 |
| `Common/Modules/SchedulerRoundRobin/SchedulerRoundRobin.c` | 入队排序与选核。锁、队列、steal 留在 SchedulerRunq.c。 |

### Student/ — 课堂可替换实现样板（默认不链）

| 文件 | 用途 |
| ---- | ---- |
| `Common/Modules/Student/FileSystemRam/FileSystemRam.c` | 学生 FS 模板（Synthetic 内存盘）。 |
| `Common/Modules/Student/FileSystemRam/FileSystemRamCompat.c` | FS=ram 时顶替仍被框架引用的 Fat* 符号。 |
| `Common/Modules/Student/PhysicalMemoryBestFit/PhysicalMemoryBestFit.c` | 学生分配模板（best-fit）。 |
| `Common/Modules/Student/SchedulerPriority/SchedulerPriority.c` | 学生调度模板。 |

## 5. `Library/` — 共享库：ELF / FAT / GPT / BlockMux / UI

### (Library 根)

| 文件 | 用途 |
| ---- | ---- |
| `Library/Block.c` | 块设备抽象（后端由 HAL 注册） |
| `Library/BlockMux.c` | Primary + MSC 双后端（PR-H-msc） |
| `Library/Bmp.c` | BI_RGB BMP → RGB888（PR-G13） |
| `Library/CString.c` | freestanding 字符串/内存例程（供 lwIP 等使用） |
| `Library/Driver.c` | 驱动注册表与 Probe/Bind/Remove 生命周期（PR-D1） |
| `Library/DriverBlock.c` | Block 类适配层（PR-D2） |
| `Library/DriverInput.c` | Input 类适配层（PR-D3；PR-H-input-mux：多 backend 聚合） |
| `Library/DriverMatch.c` | 驱动匹配表过滤（PR-DRV-match-logic） |
| `Library/DriverNet.c` | Net 类适配层（PR-D3） |
| `Library/DriverNic.c` | NIC_L2 校验（PR-N-nic-l2） |
| `Library/FontData.h` | 兼容转发（PR-D1） |
| `Library/HIDKeyboard.c` | HID 键码到 ASCII 的映射 |
| `Library/LibWrite.c` | PR-R4：Library 写槽 |
| `Library/PciNames.c` | 课用 PCI 名称表（PR-DEV-names） |
| `Library/ResFs.c` | 只读「资源卷」第二 VFS 后端（PR-F3） |
| `Library/UiAction.c` | PR-GUI-btn-action：事件分发实现。 |
| `Library/UiButton.c` | PR-GUI-btn-widget：widget 层实现。 |
| `Library/Vfs.c` | FsOps 注册与分发（PR-F1；PR-F3 多后端） |

### Elf

| 文件 | 用途 |
| ---- | ---- |
| `Library/Elf/Elf.c` | PR-S3-elf-1：ELF64 公共帮手（头校验 / PT_LOAD 映射 / 动态段） |
| `Library/Elf/ElfLoad.c` | PR-S3-elf-1：静态 ET_EXEC 装载（段 + 用户栈） |
| `Library/Elf/ElfReloc.c` | PR-S3-elf-1：RELA / JMPREL 重定位 |
| `Library/Elf/ElfSo.c` | PR-S3-elf-1：DT_NEEDED / ET_DYN 共享库装载 |

### Gpt

| 文件 | 用途 |
| ---- | ---- |
| `Library/Gpt/Gpt.c` | 查找 FAT 分区（PR-S-gpt-1） |
| `Library/Gpt/GptWrite.c` | 写出课堂 GPT 布局（PR-S-gpt-1） |

### UI

| 文件 | 用途 |
| ---- | ---- |
| `Library/UI/UI.c` | PR-S3-ui-1：几何核心（线/矩形/圆/三角 + 圆角） |
| `Library/UI/UIDraw.c` | PR-S3-ui-1：按钮 / 进度条 |
| `Library/UI/UILayout.c` | PR-S3-ui-1：命中测试 / 列表行 / 滚动条 |

## 6. `Common/Fonts/` — 内嵌点阵字体数据

| 文件 | 用途 |
| ---- | ---- |
| `Common/Fonts/FontRegistry.c` | 字体注册表与当前字体（PR-D1）+ Assets/Fonts TOYF（PR-T3） |
| `Common/Fonts/cjk16.c` | 16×16 汉字子集（PR-I18N1/I18N2） |
| `Common/Fonts/terminus10x18.c` | Terminus 10×18 点阵（摘自 Linux font_ter10x18.c） |
| `Common/Fonts/terminus16x32.c` | Terminus 16×32 点阵（摘自 Linux font_ter16x32.c） |

## 7. `Services/` — 系统服务：Shell / GUI / FS / Store / 网络上层

### Console

| 文件 | 用途 |
| ---- | ---- |
| `Services/Console/Console.c` | Shell 提示符与 Job 输出（PR-S3-console-2） |
| `Services/Console/ConsoleAlias.c` | 用户别名与 alias / unalias |
| `Services/Console/ConsoleBuiltin.c` | help / clear / echo 与内置注册 |
| `Services/Console/ConsoleCmd.c` | 命令表 / Register / 分发 |
| `Services/Console/ConsoleFocus.c` | Shell 焦点 / 开窗 / 重画（PR-S3-console-2） |
| `Services/Console/ConsoleInput.c` | 按键、回车与串口壳 |
| `Services/Console/ConsoleSbBar.c` | Shell 客户区滚动条（PR-GUI-shell-sb） |
| `Services/Console/ConsoleSbPaint.c` | Shell scrollback 重画（PR-S3-consolescroll-1） |
| `Services/Console/ConsoleSbSlot.c` | 每 Shell 窗独立 scrollback |
| `Services/Console/ConsoleScroll.c` | Shell 行缓冲 / 历史 / 滚轮（PR-S3-consolescroll-1） |
| `Services/Console/ConsoleWrite.c` | 串口与屏幕输出 |

### Db

| 文件 | 用途 |
| ---- | ---- |
| `Services/Db/Db.c` | TOYOS.DB 文本 KV（PR-DB1） |
| `Services/Db/DbFile.c` | TOYOS.DB 读盘 / 写盘（PR-S-db-1） |

### Desktop

| 文件 | 用途 |
| ---- | ---- |
| `Services/Desktop/Desktop.c` | 桌面图标 + 任务栏/开始菜单 + BMP 壁纸/图标（PR-S3-desktop-1） |
| `Services/Desktop/DesktopAppIcons.c` | PR-S-bundle-desktop：desktop=yes 动态桌面图标 |
| `Services/Desktop/DesktopClick.c` | 图标点选（PR-S3-desktopclick-1） |
| `Services/Desktop/DesktopClock.c` | 任务栏时钟 / MSC 热拔节流（PR-S3-desktop-1） |
| `Services/Desktop/DesktopGeom.c` | 图标 / 任务栏 / 菜单几何 |
| `Services/Desktop/DesktopIconDrag.c` | 桌面图标拖放 |
| `Services/Desktop/DesktopIconLayout.c` | 图标坐标读写与摆放（PR-S3-deskiconlayout-1） |
| `Services/Desktop/DesktopIconMove.c` | 图标拖移擦旧画新（PR-S3-deskiconlayout-1） |
| `Services/Desktop/DesktopIcons.c` | 图标 BMP / 布局 / 拖放移动（PR-S-desktop-split-3） |
| `Services/Desktop/DesktopMenu.c` | 开始菜单重建（系统项 + Game；Apps 见 DesktopMenuApps.c） |
| `Services/Desktop/DesktopMenuApps.c` | 开始菜单 Apps 二级（INST taskbar + 旧扁平 ELF） |
| `Services/Desktop/DesktopMenuCover.c` | 开始菜单覆盖矩形（局部刷新擦除用） |
| `Services/Desktop/DesktopMenuIcon.c` | 开始菜单行图标（关机/重启专用 BMP） |
| `Services/Desktop/DesktopNetTray.c` | 任务栏时钟左侧网络短状态（PR-N-nic-tray） |
| `Services/Desktop/DesktopPaint.c` | 桌面绘制（图标/任务栏/开始菜单）（PR-S-desktop-split-2） |
| `Services/Desktop/DesktopSample.c` | 桌面矩形重画与像素采样 |
| `Services/Desktop/DesktopTaskbar.c` | 任务栏与开始菜单绘制 |
| `Services/Desktop/DesktopTaskbarClick.c` | 任务栏 / 开始菜单点击（PR-S3-desktopclick-1） |
| `Services/Desktop/DesktopWallpaper.c` | 壁纸缓存 / DesktopBgAt / DesktopFillRect（PR-S-desktop-split-1） |

### DevicesUi

| 文件 | 用途 |
| ---- | ---- |
| `Services/DevicesUi/DevicesUi.c` | 设备管理器开窗 / 焦点 / 点击（PR-DEV-6） |
| `Services/DevicesUi/DevicesUiModel.c` | Device 表格式化 / 筛选（PR-DEV-6） |
| `Services/DevicesUi/DevicesUiPaint.c` | 三栏：筛选 / 列表 / 详情（PR-DEV-6） |
| `Services/DevicesUi/DevicesUiSummary.c` | 设备管理器「系统摘要」只读数据采集（PR-DEV-ui-summary-data） |

### EditUi

| 文件 | 用途 |
| ---- | ---- |
| `Services/EditUi/EditUi.c` | 简易编辑器核心（缓冲、开关、保存） |
| `Services/EditUi/EditUiInput.c` | 点击与按键 |
| `Services/EditUi/EditUiPaint.c` | 客户区、光标、Save 按钮 |

### FilesUi

| 文件 | 用途 |
| ---- | ---- |
| `Services/FilesUi/FilesUi.c` | 文件浏览器（PR-FB1/FB2 + PR-U1/U2/U3） |
| `Services/FilesUi/FilesUiActions.c` | Files 打开/删除/新建/改名（PR-S-filesui-split-3） |
| `Services/FilesUi/FilesUiClick.c` | 列表点击与悬停 |
| `Services/FilesUi/FilesUiKeys.c` | 键盘与滚轮 |
| `Services/FilesUi/FilesUiList.c` | 文件列表绘制（PR-F-filesui-1） |
| `Services/FilesUi/FilesUiListDetail.c` | PaintList 右栏预览（PR-F-filesui-1） |
| `Services/FilesUi/FilesUiListRows.c` | PaintList 路径头 + 文件行（PR-F-filesui-1） |
| `Services/FilesUi/FilesUiListSide.c` | PaintList 左栏卷列表（PR-F-filesui-1） |
| `Services/FilesUi/FilesUiNav.c` | Files 路径/卷侧栏/预览 |
| `Services/FilesUi/FilesUiPaint.c` | Files 绘制（PR-S-filesui-split-1） |
| `Services/FilesUi/FilesUiPreview.c` | 跳转、重载列表与预览 |

### FileSystem

| 文件 | 用途 |
| ---- | ---- |
| `Services/FileSystem/FileSystem.c` | 卷激活与路径解析（PR-S-filesystem-1） |
| `Services/FileSystem/FileSystemInit.c` | 重挂与启动（PR-S-filesystem-1） |
| `Services/FileSystem/FileSystemMount.c` | 扫描并挂上各卷（编排；PR-F-fs-1） |
| `Services/FileSystem/FileSystemMountPick.c` | 选默认卷 / 无名 ESP 命名（PR-F-fs-1） |
| `Services/FileSystem/FileSystemMountVol.c` | 单 FAT 分区挂载 + RES（PR-F-fs-1） |
| `Services/FileSystem/FileSystemOps.c` | 路径上的读写与目录操作（PR-S-filesystem-1） |
| `Services/FileSystem/FileSystemTree.c` | PR-S-bundle-fs：递归建路径 / 删树 |
| `Services/FileSystem/Install.c` | PR-FS-inst-1：Guest 安装器骨架 |

### GuiBackup

| 文件 | 用途 |
| ---- | ---- |
| `Services/GuiBackup/GuiBackup.c` | 窗备份缓冲（核心） |
| `Services/GuiBackup/GuiBackupSample.c` | 备份像素采样与上层覆盖判断 |

### GuiCompose

| 文件 | 用途 |
| ---- | ---- |
| `Services/GuiCompose/GuiCompose.c` | Present / 主题场景编排（PR-S-compose-split-1） |
| `Services/GuiCompose/GuiComposeRefresh.c` | GuiRefreshDesktop（开始菜单局部刷新） |
| `Services/GuiCompose/GuiFade.c` | PR-GUI-l3-fade：开关窗淡入淡出中间帧 |

### GuiCursor

| 文件 | 用途 |
| ---- | ---- |
| `Services/GuiCursor/GuiCursor.c` | 鼠标光标：save-under + 移动/显隐 |
| `Services/GuiCursor/GuiCursorAxis.c` | resize 光标：↔ / ↕ / 对角双向箭头 |
| `Services/GuiCursor/GuiCursorShape.c` | 光标字形：指针箭头 / ↔ / ↕ / 对角 resize |

### GuiDrag

| 文件 | 用途 |
| ---- | ---- |
| `Services/GuiDrag/GuiDrag.c` | 标题栏拖动生命周期（PR-S3-guidrag-1） |
| `Services/GuiDrag/GuiDragBackup.c` | 拖动备份缓冲与起始抓屏 |
| `Services/GuiDrag/GuiDragComposite.c` | 拖动脏区合成与脚印 |
| `Services/GuiDrag/GuiDragMove.c` | 窗位夹紧与拖移重画（PR-S3-guidrag-1） |
| `Services/GuiDrag/GuiDragSlide.c` | CopyRect 平移 + 只擦露出条（减左右拖频闪） |

### GuiDraw

| 文件 | 用途 |
| ---- | ---- |
| `Services/GuiDraw/GuiDraw.c` | 窗绘制入口（核心） |
| `Services/GuiDraw/GuiDrawChrome.c` | 标题栏、边框、关闭钮与遮挡画线 |
| `Services/GuiDraw/GuiDrawShadow.c` | 窗口右下阴影 |

### GuiFocus

| 文件 | 用途 |
| ---- | ---- |
| `Services/GuiFocus/GuiFocus.c` | 焦点窗裁剪与光标（核心） |
| `Services/GuiFocus/GuiFocusConsole.c` | Shell 输入行与是否接受输入 |

### GuiOpen

| 文件 | 用途 |
| ---- | ---- |
| `Services/GuiOpen/GuiOpen.c` | 开窗辅助 / 槽位（PR-S3-guiopen-1） |
| `Services/GuiOpen/GuiOpenApps.c` | Shell / Settings / Store / Files / Edit 开窗 |
| `Services/GuiOpen/GuiOpenClose.c` | 关窗与 Closing 等待（PR-S3-guiopen-1） |
| `Services/GuiOpen/GuiOpenDevices.c` | 设备管理器开窗（PR-DEV-6） |
| `Services/GuiOpen/GuiOpenTty.c` | 串口会话窗开窗（PR-GUI-tty-win） |

### GuiPointer

| 文件 | 用途 |
| ---- | ---- |
| `Services/GuiPointer/GuiClick.c` | 按下命中编排（PR-F-guiclick-1） |
| `Services/GuiPointer/GuiClickDesktop.c` | 桌面/菜单/托盘点击与 DESKTOP_ACTION（PR-F-guiclick-1） |
| `Services/GuiPointer/GuiClickFocus.c` | USER 命中与置顶后按窗种类分发（PR-F-guiclick-1） |
| `Services/GuiPointer/GuiPointer.c` | 鼠标队列轮询与输入锁（PR-S3-guipointer-1） |
| `Services/GuiPointer/GuiPointerMotion.c` | Store 长 IO 期间仅挪光标（PR-S3-guipointer-1） |
| `Services/GuiPointer/GuiPointerMouse.c` | 分辨率钳窗、方向键、右键与按键边沿 |
| `Services/GuiPointer/GuiPollHold.c` | 拖帧进行中排空鼠标，帧后再补边沿 |

### GuiResize

| 文件 | 用途 |
| ---- | ---- |
| `Services/GuiResize/GuiResize.c` | 右/底边与右下角拖拽改大小生命周期（PR-S3-guiresize-1） |
| `Services/GuiResize/GuiResizeBand.c` | 线框预览与尺寸计算（PR-S3-guiresize-1） |
| `Services/GuiResize/GuiResizeHit.c` | 改大小热区命中与光标外形（PR-S3-guiresize-1） |

### GuiUser

| 文件 | 用途 |
| ---- | ---- |
| `Services/GuiUser/GuiUser.c` | 用户态窗口协议（核心） |
| `Services/GuiUser/GuiUserBlit.c` | 客户区文字、按钮绘制与像素 blit |
| `Services/GuiUser/GuiUserKey.c` | 用户窗键盘入队（焦点路由） |
| `Services/GuiUser/GuiUserPoll.c` | GuiPollUserInput（关 / 按钮 / 键 / 客户区点） |

### GuiWm

| 文件 | 用途 |
| ---- | ---- |
| `Services/GuiWm/GuiHit.c` | 命中测试 / 叠放 Raise（PR-S-guiwm-split-1） |
| `Services/GuiWm/GuiWm.c` | 窗口全局态 / Init / 标题刷新（PR-S-guiwm-split-2） |

### Locale

| 文件 | 用途 |
| ---- | ---- |
| `Services/Locale/Locale.c` | PR-S3-locale-1：语言状态与对外 API |
| `Services/Locale/LocaleParse.c` | PR-S3-locale-1：catalog 文本解析与装载 |
| `Services/Locale/LocaleTable.c` | PR-S3-locale-1：MSG 键名与 en/zh 内建 fallback |

### LwIp

| 文件 | 用途 |
| ---- | ---- |
| `Services/LwIp/LwIp.c` | lwIP 初始化与轮询（NO_SYS）（PR-S3-lwip-1） |
| `Services/LwIp/LwIpConfig.c` | 从 NetConfig 绑 netif / DNS（PR-N-nic-addr） |
| `Services/LwIp/LwIpDhcp.c` | DHCP 客户端（PR-N-nic-dhcp / 前后端分离） |
| `Services/LwIp/LwIpPrivate.h` | LwIp*.c 内部（PR-N-nic-addr） |
| `Services/LwIp/LwIpSocket.c` | lwIP socket / DNS 胶水（PR-S3-lwip-1） |
| `Services/LwIp/NetConfig.c` | 静态地址配置（PR-N-nic-addr） |
| `Services/LwIp/README.md` | LwIp/ — 网络服务簇（lwIP + 地址配置 + legacy UDP） |
| `Services/LwIp/Udp.c` | 极简 UDP |

### SettingsUi

| 文件 | 用途 |
| ---- | ---- |
| `Services/SettingsUi/SettingsUi.c` | Settings 三分栏核心（全局、命中、开窗） |
| `Services/SettingsUi/SettingsUiApply.c` | 点选条目后写 Theme |
| `Services/SettingsUi/SettingsUiInput.c` | 键鼠与 Esc |
| `Services/SettingsUi/SettingsUiModel.c` | 分类/条目文案与显示模式表 |
| `Services/SettingsUi/SettingsUiPaint.c` | 三分栏绘制 |

### ShellCommands

| 文件 | 用途 |
| ---- | ---- |
| `Services/ShellCommands/ShellCommands.c` | PR-S-shell-split-3：注册汇总（命令体见 ShellCommands*.c） |
| `Services/ShellCommands/ShellCommandsAudio.c` | PR-G-audio-5：play [path]；无参=蜂鸣；WAV=PCM 48k/16 |
| `Services/ShellCommands/ShellCommandsFs.c` | PR-S3-shellfs-1：ls/cat/write/rm/mkdir/rmdir/mv + 注册汇总 |
| `Services/ShellCommands/ShellCommandsFsExtra.c` | PR-S3-shellfs-1：wrbig / vols / filestat / filesync / dirstress |
| `Services/ShellCommands/ShellCommandsFsUi.c` | PR-S3-shellfsui-1：开窗 / font / lang + 注册汇总 |
| `Services/ShellCommands/ShellCommandsFsUiStore.c` | PR-S3-shellfsui-1：CommandStore + 注册 |
| `Services/ShellCommands/ShellCommandsFsUiStoreJob.c` | PR-S3-shellfsui-1：store Job/列表帮手 |
| `Services/ShellCommands/ShellCommandsInstall.c` | install 命令（PR-S-shellfs-split-1） |
| `Services/ShellCommands/ShellCommandsNet.c` | PR-S3-shellnet-1：net / ping / dns + 注册汇总 |
| `Services/ShellCommands/ShellCommandsNetAddr.c` | net config / set ip\|gw\|dns（PR-N-nic-addr） |
| `Services/ShellCommands/ShellCommandsNetLwip.c` | PR-S3-shellnet-1：lwip on\|status\|dhcp（从 Net.c 搬家） |
| `Services/ShellCommands/ShellCommandsNetTcp.c` | PR-S3-shellnet-1：tcp + ShellOnInterrupt（从 Net.c 搬家） |
| `Services/ShellCommands/ShellCommandsNetUdp.c` | PR-S3-shellnet-1：udp listen / send（从 Net.c 搬家） |
| `Services/ShellCommands/ShellCommandsSystem.c` | PR-S3-shellsys-1：info/mem/reboot/halt/lsdev + 注册汇总 |
| `Services/ShellCommands/ShellCommandsSystemProc.c` | PR-S3-shellsys-1：exec / ps / kill / set priority / runuser |
| `Services/ShellCommands/ShellCommandsTheme.c` | set effects（PR-GUI-effects） |
| `Services/ShellCommands/ShellCommandsThread.c` | PR-U-thread：test thread（栈+TLS+同 CR3） |
| `Services/ShellCommands/ShellCommandsUsb.c` | PR-S-shell-split-1：xhci / msc Shell 命令 |

### Store

| 文件 | 用途 |
| ---- | ---- |
| `Services/Store/Store.c` | PR-S1：离线 catalog 安装；PR-S3：font/asset → Assets/ |
| `Services/Store/StoreCatalog.c` | catalog 解析与加载 |
| `Services/Store/StoreCombo.c` | 按依赖顺序装卸多包 |
| `Services/Store/StoreDb.c` | ToyDB 清单键与包类型查询 |
| `Services/Store/StoreDepends.c` | PKG 依赖解析与已装检查 |
| `Services/Store/StoreInstall.c` | 单包安装与登记 |
| `Services/Store/StoreInstallBundle.c` | PR-S-bundle-install：app → Apps/<id>/ |
| `Services/Store/StoreInstallCopy.c` | 可切片拷贝（PR-S-job-chunk） |
| `Services/Store/StoreManaged.c` | 托管载荷判定与内部删除 |
| `Services/Store/StorePkgMeta.c` | PR-S-bundle-desktop：读 Apps/<id>/PKG.TXT 桌面相关键 |
| `Services/Store/StoreQuery.c` | 已装清单与依赖查询 |
| `Services/Store/StoreRemove.c` | 卸装已装包 |

### StoreNet

| 文件 | 用途 |
| ---- | ---- |
| `Services/StoreNet/StoreNet.c` | 仓库地址与 fetch/sync |
| `Services/StoreNet/StoreNetHttp.c` | HTTP/1.0 GET（内建 TCP） |
| `Services/StoreNet/StoreNetHttpLwip.c` | HTTP/1.0 GET via lwIP socket（lwip on 后必走此路） |
| `Services/StoreNet/StoreNetParse.c` | 响应正文与 FNV-1a-32 |

### StoreUi

| 文件 | 用途 |
| ---- | ---- |
| `Services/StoreUi/StoreJob.c` | Store UI 作业入队 / 忙态 / 取消（PR-S3-storejob-1） |
| `Services/StoreUi/StoreJobShell.c` | Shell ↔ StoreJob：Shell 只做 INTERFACE（rm-exc-11） |
| `Services/StoreUi/StoreJobStatus.c` | Job 状态行文案（PR-S-job） |
| `Services/StoreUi/StoreJobStep.c` | Store 作业步进状态机（PR-S3-storejob-1） |
| `Services/StoreUi/StoreUi.c` | 商店三分栏核心（全局、开窗） |
| `Services/StoreUi/StoreUiInput.c` | 点击、悬停、装卸队列 |
| `Services/StoreUi/StoreUiModel.c` | 分类过滤、已装缓存、选中项 |
| `Services/StoreUi/StoreUiPaint.c` | 左栏 / 列表 / 详情 / 底栏按钮 |

### Tasks

| 文件 | 用途 |
| ---- | ---- |
| `Services/Tasks/Tasks.c` | GUI / Worker / 输入专核（PR-S-tasks-1） |
| `Services/Tasks/TasksHid.c` | HID 报告送进控制台（PR-S-tasks-1） |
| `Services/Tasks/TasksShell.c` | Shell 任务（PR-S-tasks-1） |

### Tcp

| 文件 | 用途 |
| ---- | ---- |
| `Services/Tcp/Tcp.c` | 自研单连接 TCP（legacy）：握手/回显 + 发送缓冲、对端窗口、超时重传 |
| `Services/Tcp/TcpInput.c` | 入站段编排（PR-F-tcp-1） |
| `Services/Tcp/TcpInputEst.c` | ESTABLISHED 入站（数据 / RST / FIN）（PR-F-tcp-1） |
| `Services/Tcp/TcpInputSyn.c` | LISTEN / SYN_SENT / SYN_RECEIVED（PR-F-tcp-1） |
| `Services/Tcp/TcpPrivate.h` | 单连接 TCP 状态（PR-S-tcp-1） |
| `Services/Tcp/TcpSend.c` | 校验、发送缓冲、重传（PR-S-tcp-1） |

### Theme

| 文件 | 用途 |
| ---- | ---- |
| `Services/Theme/Theme.c` | 主题状态、getter/setter、Apply |
| `Services/Theme/ThemeCfg.c` | 读 THEME.CFG 与 ThemeLoad |
| `Services/Theme/ThemeLive.c` | 运行时改缩放 / 分辨率 |
| `Services/Theme/ThemeParse.c` | THEME.CFG / DB 行解析 |
| `Services/Theme/ThemeSave.c` | 写 THEME.CFG 与 TOYOS.DB（编排；PR-F-theme-1） |
| `Services/Theme/ThemeSaveCfg.c` | 拼 THEME.CFG 缓冲并写盘（PR-F-theme-1） |
| `Services/Theme/ThemeSaveDb.c` | 批量写 TOYOS.DB（PR-F-theme-1） |
| `Services/Theme/ThemeSaveFmt.c` | THEME.CFG / DB 数值格式化与 FillVals |
| `Services/Theme/ThemeTech.c` | PR-GUI-tech-1：tech 色板 + chrome getter 分支 |
| `Services/Theme/ThemeTechUi.c` | PR-GUI-tech-2：文字 / 图标 / 菜单 / 面板 / 滚动条 getter |

### TtyUi

| 文件 | 用途 |
| ---- | ---- |
| `Services/TtyUi/TtyUi.c` | 串口会话缓冲 / 键入 TX |
| `Services/TtyUi/TtyUiPaint.c` | 会话缓冲绘制 |

## 8. `HAL/Board/` — 板包约定与模板

| 文件 | 用途 |
| ---- | ---- |
| `HAL/Board/README.md` | Board 包约定（PR-B0） |
| `HAL/Board/_template/BoardConfig.h.example` | （见文件名 / 同目录 README） |
| `HAL/Board/_template/NOTES.md.example` | （见文件名 / 同目录 README） |
| `HAL/Board/_template/README.md` | Board：`<board-name>`（模板 — 复制后改名） |

## 9. `HAL/X64/` — x86-64 HAL：启动、中断、页表、HalDevices、LwIp 移植

### (Arch 核心)

| 文件 | 用途 |
| ---- | ---- |
| `HAL/X64/AcpiMadt.h` | 解析 ACPI MADT，收集 Local APIC ID |
| `HAL/X64/AcpiMadt/AcpiMadt.c` | MADT 枚举与表探测（PR-S-acpimadt-1） |
| `HAL/X64/AcpiMadt/AcpiMadtDmar.c` | DMAR DRHD 关 TE（PR-S-acpimadt-1） |
| `HAL/X64/AcpiMadt/AcpiMadtPower.c` | FACP 电源初始化（PR-S-acpimadt-1） |
| `HAL/X64/AcpiMadt/AcpiMadtPrivate.h` | 表头、映射与电源态（PR-S-acpimadt-1） |
| `HAL/X64/AcpiMadt/AcpiMadtReset.c` | 软关机与复位（PR-S-acpimadt-1） |
| `HAL/X64/AcpiMadt/AcpiMadtSleep.c` | FACP/_S5_ 与 PM1 帮手（PR-S-acpimadt-1） |
| `HAL/X64/AcpiMadt/AcpiMadtTable.c` | RSDP/XSDT 查找与映射（PR-S-acpimadt-1） |
| `HAL/X64/Arch.c` | x86-64 GDT/TSS/SYSCALL 与 ArchInit（PR-S3-arch-1） |
| `HAL/X64/Arch.h` | x86-64 架构相关接口（仅 HAL 内部；Common 经 Hal* 访问） |
| `HAL/X64/ArchInterrupt.c` | x86-64 IDT / PIC / LAPIC / 中断分发（PR-S3-arch-1） |
| `HAL/X64/ArchPrivate.h` | x86 Arch.c / ArchInterrupt.c 内部交接（PR-S3-arch-1） |
| `HAL/X64/BootConfig.h` | x86 UEFI 引导参数（ToyBoot ↔ HAL/X86_64/Startup） |
| `HAL/X64/BootHandoff.h` | ToyBoot <-> Kernel handoff ABI (PR-R1) |
| `HAL/X64/DeviceEnum.c` | x86-64 设备枚举：只读扫 PCI（PR-DEV-2） |
| `HAL/X64/DeviceEnumPciTree.c` | PR-DEV-tree-pci：扁平枚举后按 PCI 桥建父子边。 |
| `HAL/X64/Interrupt.S` | x86-64 中断入口桩（汇编） |
| `HAL/X64/Io.c` | x86 端口 I/O（in/out） |
| `HAL/X64/IoApic.c` | PR-H-ioapic：I/O APIC 初始化与中断路由 |
| `HAL/X64/IoApic.h` | PR-H-ioapic：真机 I/O APIC 初始化与 GSI 路由（HAL/X64 内部） |
| `HAL/X64/NOTES-UEFI-PC.md` | x86 UEFI PC / 笔记本目标机（PR-H0） |
| `HAL/X64/PCIe.c` | PCI 配置空间与 USB 控制器枚举（PR-S3-pcie-1） |
| `HAL/X64/PCIe.h` | PCI 配置空间与 USB 控制器扫描 |
| `HAL/X64/PCIeBar.c` | PCI BAR 大小探测（PR-DEV-bar-size · 阶段 4 第 1 刀） |
| `HAL/X64/PCIeMsi.c` | PCI MSI/MSI-X 与 IOAPIC INTx（PR-S3-pcie-1） |
| `HAL/X64/PageTable.c` | HAL/X64 页表 Hal* API（PR-S3-pagetable-1） |
| `HAL/X64/PageTablePrivate.h` | HAL/X64 PageTableWalk / PageTable 内部交接（PR-S3-pagetable-1） |
| `HAL/X64/PageTableWalk.c` | HAL/X64 页表遍历与辅助（PR-S3-pagetable-1） |
| `HAL/X64/Platform.c` | x86-64 固定 MMIO 映射与 Boot 传入的设备地址 |
| `HAL/X64/Platform.h` | x86-64 平台 MMIO / 设备兜底（Common 经 Hal.h 间接使用） |
| `HAL/X64/SmpBoot.c` | MADT + AP 拉起 / CPU Id·ticks（PR-S3-smpboot-1） |
| `HAL/X64/SmpBootPrivate.h` | SmpBoot / SmpBootStart 内部交接（PR-S3-smpboot-1） |
| `HAL/X64/SmpBootStart.c` | INIT/SIPI 跳板与 StartOneAp（PR-S3-smpboot-1） |
| `HAL/X64/SmpTrampoline.S` | AP 实模式 → 长模式（链接地址 0x8000，由 BSP 拷贝） |
| `HAL/X64/SmpTrampoline.ld` | SmpTrampoline.ld — AP 启动桩链接到物理 0x8000（再由 BSP 拷贝） |
| `HAL/X64/Startup.c` | x86-64 UEFI 入口：BOOT_CONFIG → BOOT_INFO → KernelMain |
| `HAL/X64/SyscallEntry.S` | SYSCALL/SYSRET 快速路径（与 int 0x80 / Isr128 解耦） |
| `HAL/X64/link.ld` | link.ld — 内核链接脚本 |

### Hal

| 文件 | 用途 |
| ---- | ---- |
| `HAL/X64/Hal/Hal.c` | PR-S3-hal-x64-1：CPU / IRQ / Timer / 用户地址布局 |
| `HAL/X64/Hal/HalConsole.c` | 控制台门面（串口 + 帧缓冲文字） |
| `HAL/X64/Hal/HalDevices.c` | x86：注册 / USB·MSC / Wifi·Iwl / Igpu·Hda（PR-S3-haldev-1） |
| `HAL/X64/Hal/HalDevicesInput.c` | x86：Input / EHCI·UHCI·PS2 诊断门面（PR-S3-haldev-1） |
| `HAL/X64/Hal/HalDevicesNet.c` | x86：Net 门面（PR-S3-haldev-1） |
| `HAL/X64/Hal/HalFrame.c` | PR-S3-hal-x64-1：中断帧 / TLS / 用户进入 |
| `HAL/X64/Hal/HalPlat.c` | PR-S3-hal-x64-1：架构名 / ELF / 调试 / 平台探测 |
| `HAL/X64/Hal/HalPort.h` | 对 Common 可见的架构布局（无 Arch* 原型，PR-A5） |
| `HAL/X64/Hal/HalRtc.c` | PR-G-taskbar-clock：墙钟（CMOS 0x70/71） |
| `HAL/X64/Hal/HalSerial/HalSerial.c` | 调试日志门面 |
| `HAL/X64/Hal/HalSerial/HalSerialGop.c` | 开机屏上滚（PR-S-halserial-1） |
| `HAL/X64/Hal/HalSerial/HalSerialPrivate.h` | 开机日志与串口门面（PR-S-halserial-1） |
| `HAL/X64/Hal/HalSerial/HalSerialWrite.c` | 通道过滤与写出（PR-S-halserial-1） |
| `HAL/X64/Hal/HalVideo.c` | PR-S3-halvideo-1：模式 / Present / 后缓冲 / 缩放 |
| `HAL/X64/Hal/HalVideoDraw.c` | PR-S3-halvideo-1：像素 / 矩形 / 文字 / 裁剪 |
| `HAL/X64/Hal/HalVideoFb.c` | PR-S3-halvideo-1：FB WC 映射与 PTE 诊断 |

### LwIp

| 文件 | 用途 |
| ---- | ---- |
| `HAL/X64/LwIp/include/arch/cc.h` | GCC freestanding types for lwIP |
| `HAL/X64/LwIp/include/arch/sys_arch.h` | NO_SYS: no OS primitives required |
| `HAL/X64/LwIp/include/lwipopts.h` | ToyOS lwIP (NO_SYS raw API, IPv4 only) |
| `HAL/X64/LwIp/toy_ip.h` | （见文件名 / 同目录 README） |
| `HAL/X64/LwIp/toy_netif.c` | virtio-net 以太网 netif（linkoutput → NetSendEthernet） |
| `HAL/X64/LwIp/toy_netif.h` | 已 Add 后改地址；成功 0 */ |
| `HAL/X64/LwIp/toy_ping.c` | lwIP 单次 ICMP echo（NO_SYS raw API） |
| `HAL/X64/LwIp/toy_ping.h` | （见文件名 / 同目录 README） |
| `HAL/X64/LwIp/toy_socket.c` | lwIP 持久 TCP socket（NO_SYS raw API） |
| `HAL/X64/LwIp/toy_socket.h` | lwIP 持久 TCP socket（用户态 SYS_SOCKET 后端） |
| `HAL/X64/LwIp/toy_tcpclient.c` | lwIP 主动 TCP 连接并发送（NO_SYS raw API） |
| `HAL/X64/LwIp/toy_tcpclient.h` | 0 ok; -1 connect err; -2 timeout; -3 send err */ |
| `HAL/X64/LwIp/toy_tcpecho.c` | lwIP TCP echo（改编自 contrib/apps/tcpecho_raw） |
| `HAL/X64/LwIp/toy_tcpecho.h` | （见文件名 / 同目录 README） |
| `HAL/X64/LwIp/toy_udp.c` | lwIP UDP bind / send / recv queue（NO_SYS raw API） |
| `HAL/X64/LwIp/toy_udp.h` | （见文件名 / 同目录 README） |

## 10. `HAL/X64/Drivers/` — x86 设备驱动（一设备一夹）

### (Drivers 根公开头 / Demo·Serial)

| 文件 | 用途 |
| ---- | ---- |
| `HAL/X64/Drivers/Ahci.h` | AHCI HBA 块设备（PR-H1：第二 Block 后端） |
| `HAL/X64/Drivers/Alx.h` | Qualcomm Atheros alx（AR8161…）对外 API（PR-N-alx-2） |
| `HAL/X64/Drivers/DemoDriver.c` | 课堂 Demo（PR-D-tpl-2 / PR-DRV-match-demo） |
| `HAL/X64/Drivers/E1000.h` | Intel 8254x / 82574 L2（PR-H4 / H4e-1…3） |
| `HAL/X64/Drivers/Ehci.h` | EHCI 对外 API（PR-H-ehci-1/2/3） |
| `HAL/X64/Drivers/Hda.h` | Intel HD Audio（PR-G-audio） |
| `HAL/X64/Drivers/Igpu.h` | Intel 核显（PR-G-igpu-0…4） |
| `HAL/X64/Drivers/Iwl.h` | Intel 8265/8275 对外 API（PR-N-wifi-2） |
| `HAL/X64/Drivers/Net.h` | virtio-net 驱动与 IPv4/ARP/ICMP，并为 UDP/TCP 提供发送入口（PR-D3） |
| `HAL/X64/Drivers/Nvme.h` | PCIe NVMe 块设备（PR-H5） |
| `HAL/X64/Drivers/Rtl.h` | Realtek r8169（RTL8168/8111…）对外 API（PR-N-rtl-2） |
| `HAL/X64/Drivers/Serial.c` | COM1 串口驱动（经 HalIo；PR-H3：探测存在性） |
| `HAL/X64/Drivers/Serial.h` | COM1 串口驱动（仅 HAL 内部使用，Common 请用 HalSerial.h） |
| `HAL/X64/Drivers/Uhci.h` | UHCI 对外 API（PR-H-uhci-1） |
| `HAL/X64/Drivers/Video.h` | GOP 帧缓冲驱动（仅 HAL 内部使用，Common 请用 HalVideo.h） |
| `HAL/X64/Drivers/Wifi.h` | USB RTL8188EU 对外 API（PR-N-wifi-1） |
| `HAL/X64/Drivers/XHCI.h` | USB 3.0 xHCI 主机控制器驱动接口 |

### _template

| 文件 | 用途 |
| ---- | ---- |
| `HAL/X64/Drivers/_template/README.md` | .c)` |
| `HAL/X64/Drivers/_template/Template.c` | 拷贝源（PR-D-tpl-1） |
| `HAL/X64/Drivers/_template/TemplateNetL2.c` | 网卡 L2 注释骨架（PR-N-nic-doc） |

### Ahci

| 文件 | 用途 |
| ---- | ---- |
| `HAL/X64/Drivers/Ahci/Ahci.c` | 最小 AHCI 读写（PR-H1） |
| `HAL/X64/Drivers/Ahci/AhciInitialize.c` | PCI 查找与端口初始化（PR-S-ahci-1） |
| `HAL/X64/Drivers/Ahci/AhciPrivate.h` | 寄存器、端口与驱动态（PR-S-ahci-1） |
| `HAL/X64/Drivers/Ahci/AhciTransfer.c` | 扇区 DMA 传送（PR-S-ahci-1） |
| `HAL/X64/Drivers/Ahci/BlockAhci.c` | AHCI 块设备经 Driver Block 类注册（PR-H1） |

### Alx

| 文件 | 用途 |
| ---- | ---- |
| `HAL/X64/Drivers/Alx/Alx.c` | TPD/RFD/RRD 环 + SendFrame/Poll（PR-N-alx-2） |
| `HAL/X64/Drivers/Alx/AlxHw.c` | MAC/PHY 复位 / 基本配置 / 开 TX·RX（PR-N-alx-2） |
| `HAL/X64/Drivers/Alx/AlxPhy.c` | MDIO + 链路（PR-N-alx-2） |
| `HAL/X64/Drivers/Alx/AlxPrivate.h` | 寄存器 / 描述符 / 跨文件状态（PR-N-alx-1/2） |
| `HAL/X64/Drivers/Alx/AlxProbe.c` | PCI 查找、BAR、永久 MAC（PR-N-alx-1）；Setup 末尾 BringUp（alx-2） |
| `HAL/X64/Drivers/Alx/NetAlx.c` | alx 经 Driver Net 类注册（PR-N-alx-2） |

### Ata

| 文件 | 用途 |
| ---- | ---- |
| `HAL/X64/Drivers/Ata/Ata.c` | ATA PIO 模式磁盘读写（Primary Master/Slave，经 HalIo） |
| `HAL/X64/Drivers/Ata/Ata.h` | ATA PIO 磁盘驱动接口 |
| `HAL/X64/Drivers/Ata/BlockAta.c` | x86 ATA PIO 块设备（PR-D2：经 Driver Block 类注册） |

### E1000

| 文件 | 用途 |
| ---- | ---- |
| `HAL/X64/Drivers/E1000/E1000.c` | Intel e1000 收发 / 链路查询（PR-S3-e1000-2） |
| `HAL/X64/Drivers/E1000/E1000Mac.c` | NVM / RAL MAC（PR-S3-e1000probe-1） |
| `HAL/X64/Drivers/E1000/E1000Note.c` | PR-N-i219-note：只读现场 dump（不加 DID、不改 TX/PHY） |
| `HAL/X64/Drivers/E1000/E1000Private.h` | 寄存器、描述符、跨文件状态（PR-S-e1000-1） |
| `HAL/X64/Drivers/E1000/E1000Probe.c` | PCI 查找、I219 调参、MSI / 链路（PR-S3-e1000probe-1） |
| `HAL/X64/Drivers/E1000/E1000Setup.c` | e1000 BAR / 环 / 链路 Setup（PR-S3-e1000-2） |
| `HAL/X64/Drivers/E1000/NetE1000.c` | e1000 / e1000e 经 Driver Net 类注册（PR-H4 / PR-N-nic-e1000） |

### Ehci

| 文件 | 用途 |
| ---- | ---- |
| `HAL/X64/Drivers/Ehci/EhciBulk.c` | 异步 Bulk（PR-H-ehci-3） |
| `HAL/X64/Drivers/Ehci/EhciEnum.c` | 根口 / 设备枚举 boot HID（PR-H-ehci-2 · 2j） |
| `HAL/X64/Drivers/Ehci/EhciEnumDevice.c` | 单设备 HID 枚举（PR-H-ehci-2） |
| `HAL/X64/Drivers/Ehci/EhciFtdi.c` | Claim 入口 + Bulk TX/RX tee（PR-H-ehci-4） |
| `HAL/X64/Drivers/Ehci/EhciFtdiClaim.c` | EHCI 上认 FT232（PR-H-ehci-4） |
| `HAL/X64/Drivers/Ehci/EhciHid.c` | Bringup + 武装中断 IN（PR-H-ehci-2） |
| `HAL/X64/Drivers/Ehci/EhciHidPoll.c` | 报告入队 + dequeue（PR-H-ehci-2） |
| `HAL/X64/Drivers/Ehci/EhciHub.c` | 单层 HS hub（Intel RMH）下游枚举（PR-H-ehci-2 · 2l） |
| `HAL/X64/Drivers/Ehci/EhciHw.c` | 单控制器 handoff / 复位 / CCS（PR-H-ehci-1） |
| `HAL/X64/Drivers/Ehci/EhciMsc.c` | MSC 门面：Ready / Scan / Claim / Release / Present（PR-H-ehci-3） |
| `HAL/X64/Drivers/Ehci/EhciMscBot.c` | BOT + capacity + 扇区（PR-H-ehci-3） |
| `HAL/X64/Drivers/Ehci/EhciMscClaim.c` | Parse Bulk + FinishClaim / hub 子口（PR-H-ehci-3） |
| `HAL/X64/Drivers/Ehci/EhciMscHub.c` | MSC 用 hub 控制（PR-H-ehci-3） |
| `HAL/X64/Drivers/Ehci/EhciPort.c` | 根口复位（PR-H-ehci-2） |
| `HAL/X64/Drivers/Ehci/EhciPrivate.h` | EHCI 寄存器 / QH·qTD（PR-H-ehci-1/2） |
| `HAL/X64/Drivers/Ehci/EhciProbe.c` | 扫 PCI EHCI 并起控制器（PR-H-ehci-1） |
| `HAL/X64/Drivers/Ehci/EhciSched.c` | DMA（UC）+ reclaim 头 + CtrlQH + ASE（PR-H-ehci-2 · 2h） |
| `HAL/X64/Drivers/Ehci/EhciWifiClaim.c` | EHCI 上认 RTL8188EU（PR-N-wifi-1） |
| `HAL/X64/Drivers/Ehci/EhciXfer.c` | 异步 control（停 ASE → 挂 qTD → 开 ASE）（PR-H-ehci-2 · 2h） |
| `HAL/X64/Drivers/Ehci/InputEhci.c` | ehci 经 Driver Input 类注册（PR-H-ehci-1/2） |
| `HAL/X64/Drivers/Ehci/InputEhci.h` | EHCI 经 Driver Input 类注册（PR-H-ehci-1） |

### Hda

| 文件 | 用途 |
| ---- | ---- |
| `HAL/X64/Drivers/Hda/Hda.c` | PR-G-audio-0：Intel HD Audio PCI 认卡（NUC7 / QEMU intel-hda） |
| `HAL/X64/Drivers/Hda/HdaCodec.c` | PR-G-audio-2：枚举 codec/输出 pin。本课 NUC 无耳机孔=HDMI 主路径。 |
| `HAL/X64/Drivers/Hda/HdaCorb.c` | PR-G-audio-2：控制器复位 + CORB/RIRB（poll，不写播放流） |
| `HAL/X64/Drivers/Hda/HdaHdmi.c` | Intel HDMI/DP：开 pin、选路、DIP、显示侧 AUD 使能 |
| `HAL/X64/Drivers/Hda/HdaMmio.c` | PR-G-audio-1：BAR0 UC 映入 + 只读控制器指纹（不写 MMIO） |
| `HAL/X64/Drivers/Hda/HdaStream.c` | Stream+BDL+DMA（HDMI/DP）；audio-3 建路，audio-4 复用播放 |

### Igpu

| 文件 | 用途 |
| ---- | ---- |
| `HAL/X64/Drivers/Igpu/Igpu.c` | PR-G-igpu-0：Intel VGA/Display PCI 认卡（NUC7 UHD 620） |
| `HAL/X64/Drivers/Igpu/IgpuBlit.c` | BCS ring + MI_NOOP（PR-S3-igpublit-1） |
| `HAL/X64/Drivers/Igpu/IgpuBlitPrivate.h` | IgpuBlit / IgpuBlitTest 内部交接（PR-S3-igpublit-1） |
| `HAL/X64/Drivers/Igpu/IgpuBlitTest.c` | BCS mem/色块自检（PR-S3-igpublit-1） |
| `HAL/X64/Drivers/Igpu/IgpuForcewake.c` | PR-G-igpu-3：Gen9 Render/GT forcewake（对照 i915） |
| `HAL/X64/Drivers/Igpu/IgpuGsm.c` | PR-G-igpu-3：Gen8+ GSM（GGTT PTE 表在 BAR 后半） |
| `HAL/X64/Drivers/Igpu/IgpuGtt.c` | PR-G-igpu-2/3：观察固件 scanout（不写 GGTT PTE） |
| `HAL/X64/Drivers/Igpu/IgpuMmio.c` | PR-G-igpu-1：BAR0 UC 映入 + 只读指纹（不写 MMIO、不提交） |
| `HAL/X64/Drivers/Igpu/IgpuPresent.c` | PR-G-igpu-4：后缓冲映 GGTT + XY_SRC_COPY → gtt0 scanout |
| `HAL/X64/Drivers/Igpu/IgpuPresentPrep.c` | 启动期映后缓冲，SRC_COPY→gtt0 探针（igpu-4） |
| `HAL/X64/Drivers/Igpu/IgpuScanout.c` | igpu-5：双缓冲 + vblank 翻页，避免写入正在扫描的 FB |

### Iwl

| 文件 | 用途 |
| ---- | ---- |
| `HAL/X64/Drivers/Iwl/Iwl.c` | Setup / 状态 / 后台起站（PR-S-iwl-split-2） |
| `HAL/X64/Drivers/Iwl/IwlAes.c` | AES-128 encrypt one 16-byte block (teaching-minimal Rijndael) |
| `HAL/X64/Drivers/Iwl/IwlAlive.c` | 等 ALIVE；解析 scd_base_ptr；排空 RX |
| `HAL/X64/Drivers/Iwl/IwlAssoc/IwlAssoc.c` | 关联入口（PR-S-iwl-split-4） |
| `HAL/X64/Drivers/Iwl/IwlAssoc/IwlAssocInternal.h` | 关联跨文件（PR-S-iwl-split-4） |
| `HAL/X64/Drivers/Iwl/IwlAssoc/IwlAssocRsn.c` | STA RSN IE（PR-S-iwl-split-4） |
| `HAL/X64/Drivers/Iwl/IwlAssoc/IwlAssocUtil.c` | 关联小工具（PR-S-iwl-split-4） |
| `HAL/X64/Drivers/Iwl/IwlAssoc/IwlAssocWait.c` | Auth/Assoc 收包等待（PR-S-iwl-split-4） |
| `HAL/X64/Drivers/Iwl/IwlCcmp.c` | 802.11 CCMP (AES-CCM, M=8, L=2) teaching-minimal |
| `HAL/X64/Drivers/Iwl/IwlCfg.c` | 读 FW/WIFI.CFG（SSID= / PSK=）；永不日志 PSK |
| `HAL/X64/Drivers/Iwl/IwlDataRx.c` | 数据面解密与 NetInput（PR-S-iwl-split-2，自 Iwl.c 搬家） |
| `HAL/X64/Drivers/Iwl/IwlEapol/IwlEapol.c` | WPA2-PSK 四次握手入口（PR-N-wifi-2；PR-S-iwl-split-1 瘦身） |
| `HAL/X64/Drivers/Iwl/IwlEapol/IwlEapolCrypto.c` | WPA2-PSK 四次握手辅助（PR-S-iwl-split-1，自 IwlEapol.c 搬家） |
| `HAL/X64/Drivers/Iwl/IwlEapol/IwlEapolHand.c` | WPA2-PSK 四次握手辅助（PR-S-iwl-split-1，自 IwlEapol.c 搬家） |
| `HAL/X64/Drivers/Iwl/IwlEapol/IwlEapolIo.c` | WPA2-PSK 四次握手辅助（PR-S-iwl-split-1，自 IwlEapol.c 搬家） |
| `HAL/X64/Drivers/Iwl/IwlEapol/IwlEapolM1.c` | 等 EAPOL msg1（PR-S-iwl-split-1） */ |
| `HAL/X64/Drivers/Iwl/IwlEapol/IwlEapolUtil.c` | WPA2-PSK 四次握手辅助（PR-S-iwl-split-1，自 IwlEapol.c 搬家） |
| `HAL/X64/Drivers/Iwl/IwlFh.c` | FH 服务通道灌固件（PR-N-wifi-2） |
| `HAL/X64/Drivers/Iwl/IwlFhDiag.c` | FH 可达性探针 + 失败黄字（PR-N-wifi-2 短刀） |
| `HAL/X64/Drivers/Iwl/IwlFw.c` | 读盘 / TLV / INIT→RT 编排（PR-N-wifi-2） |
| `HAL/X64/Drivers/Iwl/IwlHw.c` | MMIO / nic lock / prepare / APM / start_hw（PR-N-wifi-2） |
| `HAL/X64/Drivers/Iwl/IwlLog.c` | iwl 黄字（PR-S-iwl-split-2，自 Iwl.c 搬家） |
| `HAL/X64/Drivers/Iwl/IwlMac.c` | 刀 #133/#134：WFMP 网卡地址。 |
| `HAL/X64/Drivers/Iwl/IwlMvm/IwlMvm.c` | scan_cfg / MCC / post_alive（PR-S-iwl-split-3） |
| `HAL/X64/Drivers/Iwl/IwlMvm/IwlMvmAssoc.c` | MAC assoc / LQ / 固件钥（PR-S-iwl-split-3） |
| `HAL/X64/Drivers/Iwl/IwlMvm/IwlMvmCtxt.c` | PHY/MAC/STA/binding（PR-S-iwl-split-3） |
| `HAL/X64/Drivers/Iwl/IwlMvm/IwlMvmInternal.h` | MVM 跨文件辅助（PR-S-iwl-split-3） |
| `HAL/X64/Drivers/Iwl/IwlMvm/IwlMvmUtil.c` | MVM 小工具（PR-S-iwl-split-3） |
| `HAL/X64/Drivers/Iwl/IwlPaging.c` | CPU2 FW paging（刀 #24） |
| `HAL/X64/Drivers/Iwl/IwlPbkdf.c` | PBKDF2-HMAC-SHA1 for WPA-PSK (teaching-minimal) |
| `HAL/X64/Drivers/Iwl/IwlPhy/IwlPhy.c` | PHY 状态 / 下发 CFG+ANT（PR-S-iwl-split-5） |
| `HAL/X64/Drivers/Iwl/IwlPhy/IwlPhyCalib.c` | INIT 校准收集（PR-S-iwl-split-5） |
| `HAL/X64/Drivers/Iwl/IwlPhy/IwlPhyDb.c` | phy_db 下发（PR-S-iwl-split-5） |
| `HAL/X64/Drivers/Iwl/IwlPhy/IwlPhyInternal.h` | PHY DB 跨文件（PR-S-iwl-split-5） |
| `HAL/X64/Drivers/Iwl/IwlPoll.c` | RX 轮询与链路查询（PR-S-iwl-split-2，自 Iwl.c 搬家） |
| `HAL/X64/Drivers/Iwl/IwlPrivate.h` | wifi-2 内部状态（对照 FreeBSD iwm / Linux iwlwifi，只读） |
| `HAL/X64/Drivers/Iwl/IwlProbe.c` | PCI 8086:24fd + BAR0 Map（PR-N-wifi-2） |
| `HAL/X64/Drivers/Iwl/IwlRegs.h` | 8265/family-8000 CSR·FH·PRPH（对照 FreeBSD if_iwmreg.h，只读） |
| `HAL/X64/Drivers/Iwl/IwlRx/IwlRx.c` | RX 环（PR-S-iwl-split-5） |
| `HAL/X64/Drivers/Iwl/IwlRx/IwlRxHold.c` | MPDU Hold 环（PR-S-iwl-split-5） |
| `HAL/X64/Drivers/Iwl/IwlRx/IwlRxInternal.h` | RX Hold 跨文件（PR-S-iwl-split-5） |
| `HAL/X64/Drivers/Iwl/IwlScan/IwlScan.c` | 扫描入口（PR-S-iwl-split-4） |
| `HAL/X64/Drivers/Iwl/IwlScan/IwlScanBeacon.c` | beacon 解析（PR-S-iwl-split-4） |
| `HAL/X64/Drivers/Iwl/IwlScan/IwlScanBuild.c` | UMAC SCAN_REQ（PR-S-iwl-split-4） |
| `HAL/X64/Drivers/Iwl/IwlScan/IwlScanCollect.c` | 扫描收包环（PR-S-iwl-split-4） |
| `HAL/X64/Drivers/Iwl/IwlScan/IwlScanInternal.h` | 扫描跨文件（PR-S-iwl-split-4） |
| `HAL/X64/Drivers/Iwl/IwlScan/IwlScanUtil.c` | 扫描小工具（PR-S-iwl-split-4） |
| `HAL/X64/Drivers/Iwl/IwlSend.c` | 以太网→802.11 发送（PR-S-iwl-split-2，自 Iwl.c 搬家） |
| `HAL/X64/Drivers/Iwl/IwlSha1.c` | SHA-1 and HMAC-SHA1 (teaching-minimal, no libc) |
| `HAL/X64/Drivers/Iwl/IwlSha256.c` | SHA-256 与 802.11 KDF（描述符版本 3 的 PTK） |
| `HAL/X64/Drivers/Iwl/IwlTx/IwlTx.c` | TX 状态 / 辅助 / TxInit / NicInit（PR-S-iwl-split-2） |
| `HAL/X64/Drivers/Iwl/IwlTx/IwlTxAux.c` | AUX/AP 队列与 post_alive SCD（PR-S-iwl-split-2） |
| `HAL/X64/Drivers/Iwl/IwlTx/IwlTxCmd.c` | host command 入队与同步等回（PR-S-iwl-split-2） |
| `HAL/X64/Drivers/Iwl/IwlTx/IwlTxCmdq.c` | CMD 队列诊断 / 解楔 / FH 启停（PR-S-iwl-split-2） |
| `HAL/X64/Drivers/Iwl/IwlTx/IwlTxFrame.c` | 802.11 帧经 AUX/AP 队列发送（PR-S-iwl-split-2） |
| `HAL/X64/Drivers/Iwl/IwlTx/IwlTxInternal.h` | TX 队列跨文件状态（PR-S-iwl-split-2，仅 IwlTx*.c 包含） |
| `HAL/X64/Drivers/Iwl/IwlWrap.c` | AES-128 解密与 RFC 3394 拆包（WPA2 组密钥） |
| `HAL/X64/Drivers/Iwl/NetIwl.c` | iwl8265 Bind → NetAttachNic（PR-N-wifi-2；需 WPA2 关联） |

### Msc

| 文件 | 用途 |
| ---- | ---- |
| `HAL/X64/Drivers/Msc/BlockMsc.c` | USB MSC 经 Driver Block（PR-H-msc） |
| `HAL/X64/Drivers/Msc/UsbMsc.c` | BOT 门面（PR-H-msc-2…7b + PR-H-ehci-3） |
| `HAL/X64/Drivers/Msc/UsbMsc.h` | USB MSC BOT 门面（PR-H-msc） |

### Net

| 文件 | 用途 |
| ---- | ---- |
| `HAL/X64/Drivers/Net/Net.c` | 发送帧与地址查询（PR-S-net-1） |
| `HAL/X64/Drivers/Net/NetAddr.c` | IP 文本与查询（PR-S-net-1） |
| `HAL/X64/Drivers/Net/NetArp.c` | ARP / ICMP / ping（PR-S-net-1） |
| `HAL/X64/Drivers/Net/NetNic.c` | NetAttachNic / L2 分发（PR-N-nic-attach） |
| `HAL/X64/Drivers/Net/NetPrivate.h` | Net 协议核 + VirtioNet 队列共享（Drivers/Net、Drivers/VirtioNet） |
| `HAL/X64/Drivers/Net/NetRx.c` | 收包与轮询（PR-S-net-1） |
| `HAL/X64/Drivers/Net/README.md` | Net/ — 协议核（非设备 L2） |

### Nvme

| 文件 | 用途 |
| ---- | ---- |
| `HAL/X64/Drivers/Nvme/BlockNvme.c` | NVMe 块设备经 Driver Block 类注册（PR-H5） |
| `HAL/X64/Drivers/Nvme/Nvme.c` | 最小 NVMe 读写（PR-H5） |
| `HAL/X64/Drivers/Nvme/NvmeCommand.c` | Admin/IO 命令与扇区传送（PR-S-nvme-1） |
| `HAL/X64/Drivers/Nvme/NvmeInitialize.c` | 控制器初始化与 PCI BAR（PR-S-nvme-1） |
| `HAL/X64/Drivers/Nvme/NvmePrivate.h` | 寄存器、队列与控制器态（PR-S-nvme-1） |

### Ps2

| 文件 | 用途 |
| ---- | ---- |
| `HAL/X64/Drivers/Ps2/InputPs2.c` | i8042 经 Driver Input 类（PR-H2 + PR-H-ps2-aux） |
| `HAL/X64/Drivers/Ps2/InputPs2.h` | i8042 PS/2 键盘 + Aux 触控板（PR-H2 / PR-H-ps2-aux） |
| `HAL/X64/Drivers/Ps2/Ps2Aux.c` | Aux 流模式 3B 包 → 屏幕像素（PR-H-ps2-aux） |
| `HAL/X64/Drivers/Ps2/Ps2AuxDiag.c` | Shell 诊断 / retry（PR-H-ps2-aux） |
| `HAL/X64/Drivers/Ps2/Ps2Hw.c` | i8042 端口 / CCB / 键盘+Aux 上电（PR-H-ps2-aux） |
| `HAL/X64/Drivers/Ps2/Ps2Kbd.c` | Scan Code Set 2 → HID 报告（PR-H-ps2-aux） |
| `HAL/X64/Drivers/Ps2/Ps2Private.h` | i8042 键盘 + Aux 触控板（PR-H-ps2-aux） |

### Rtl

| 文件 | 用途 |
| ---- | ---- |
| `HAL/X64/Drivers/Rtl/NetRtl.c` | r8169 经 Driver Net 类注册（PR-N-rtl-2） |
| `HAL/X64/Drivers/Rtl/Rtl.c` | TX/RX 环 + SendFrame/Poll（PR-N-rtl-2） |
| `HAL/X64/Drivers/Rtl/RtlHw.c` | 复位 / 配环地址 / 开 TX·RX / 链路（PR-N-rtl-2） |
| `HAL/X64/Drivers/Rtl/RtlPrivate.h` | 寄存器 / 描述符（PR-N-rtl-2） |
| `HAL/X64/Drivers/Rtl/RtlProbe.c` | PCI 查找、BAR、MAC（PR-N-rtl-1） |

### Uhci

| 文件 | 用途 |
| ---- | ---- |
| `HAL/X64/Drivers/Uhci/InputUhci.c` | uhci 经 Driver 注册（PR-H-uhci-1：仅 Probe/CCS） |
| `HAL/X64/Drivers/Uhci/InputUhci.h` | UHCI 经 Driver 注册（PR-H-uhci-1） |
| `HAL/X64/Drivers/Uhci/UhciHw.c` | 单控制器复位 / 帧表 / CCS（PR-H-uhci-1） |
| `HAL/X64/Drivers/Uhci/UhciPrivate.h` | UHCI 1.1 I/O 寄存器（PR-H-uhci-1） |
| `HAL/X64/Drivers/Uhci/UhciProbe.c` | 扫 PCI UHCI 并起控制器（PR-H-uhci-1） |

### Video

| 文件 | 用途 |
| ---- | ---- |
| `HAL/X64/Drivers/Video/Video.c` | GOP 帧缓冲驱动（PR-G9：可选 backbuffer + 脏矩形 Present） |
| `HAL/X64/Drivers/Video/VideoBlit.c` | 矩形拷贝（PR-S-video-1） |
| `HAL/X64/Drivers/Video/VideoBochs.c` | Bochs/QEMU VBE DISPI 热切（PR-H-video-split-1） |
| `HAL/X64/Drivers/Video/VideoFill.c` | 矩形填充（PR-S-video-1） |
| `HAL/X64/Drivers/Video/VideoGlyph.c` | 字符与点阵（PR-S-video-1） |
| `HAL/X64/Drivers/Video/VideoGop.c` | PR-G-hotres-pc：真机 GOP 热切策略 |
| `HAL/X64/Drivers/Video/VideoPixel.c` | 像素读写与混合（PR-S-video-1） |
| `HAL/X64/Drivers/Video/VideoPresent.c` | 脏矩形 / 光标叠层 / Present 编排（PR-S3-videopresent-1） |
| `HAL/X64/Drivers/Video/VideoPresentRect.c` | Present 条带 blit（PR-S3-videopresent-1） |
| `HAL/X64/Drivers/Video/VideoPrivate.h` | Video 驱动内部共享头（仅 HAL/X64/Drivers/Video*.c） |
| `HAL/X64/Drivers/Video/VideoScale.c` | UI 缩放 / 逻辑分辨率 / VideoSet（PR-H-video-split-1） |
| `HAL/X64/Drivers/Video/VideoText.c` | 滚动与光标文本（PR-S-video-1） |

### VirtioNet

| 文件 | 用途 |
| ---- | ---- |
| `HAL/X64/Drivers/VirtioNet/NetDriver.c` | virtio-net 驱动注册（PR-S-net-1） |
| `HAL/X64/Drivers/VirtioNet/NetVirtio.c` | virtio-net 队列 / PCI 启动（PR-H-net-split-1） |
| `HAL/X64/Drivers/VirtioNet/NetVirtioStart.c` | PCI 能力与启动（PR-S-virtionet-1） |

### Wifi

| 文件 | 用途 |
| ---- | ---- |
| `HAL/X64/Drivers/Wifi/NetWifi.c` | rtl8188eu 经 Driver Net 类注册（PR-N-wifi-1） |
| `HAL/X64/Drivers/Wifi/Wifi.c` | 认领编排 + 状态（PR-N-wifi-1） |
| `HAL/X64/Drivers/Wifi/WifiFw.c` | 固件钩子（PR-N-wifi-1） |
| `HAL/X64/Drivers/Wifi/WifiPrivate.h` | wifi-1 内部 |

### XHCI

| 文件 | 用途 |
| ---- | ---- |
| `HAL/X64/Drivers/XHCI/InputXhci.c` | xHCI HID 后端与试 BAR（PR-S3-inputxhci-1） |
| `HAL/X64/Drivers/XHCI/InputXhci.h` | x86 xHCI HID 经 Driver Input 类（PR-D3） |
| `HAL/X64/Drivers/XHCI/InputXhciPrivate.h` | InputXhci / InputXhciProbe 内部交接（PR-S3-inputxhci-1） |
| `HAL/X64/Drivers/XHCI/InputXhciProbe.c` | xHCI HID 控制器普查 / Probe（PR-S3-inputxhci-1） |
| `HAL/X64/Drivers/XHCI/Xhci.c` | Init/Ready/Abandon 与共享全局（Keyboard/Device/Controller/Transfer/Command/Ring/Mmio 已拆） |
| `HAL/X64/Drivers/XHCI/XhciCdcClaim.c` | PR-H-usb-uart-cdc-1：CDC-ACM 认领（Comm 0x02/02/01 + Data 0x0A） |
| `HAL/X64/Drivers/XHCI/XhciCdcRx.c` | PR-H-usb-uart-cdc-2：CDC Bulk IN → 字符环（无状态前缀） |
| `HAL/X64/Drivers/XHCI/XhciCdcTx.c` | PR-H-usb-uart-cdc-1：CDC Bulk OUT TX + ConfigEP + 事件匹配 |
| `HAL/X64/Drivers/XHCI/XhciCommand.c` | PR-H-xhci-core-split-3：Command / Recover / WaitCommand |
| `HAL/X64/Drivers/XHCI/XhciController.c` | PR-H-xhci-core-split-5：TakeLegacy / Halt* / Reset / Start / BootMarkRs |
| `HAL/X64/Drivers/XHCI/XhciDevice.c` | PR-S-xhcidevice-1：Address / Slot / Ep0 环选择 |
| `HAL/X64/Drivers/XHCI/XhciDeviceControl.c` | PR-S-xhcidevice-1：ControlXfer / GetDesc / Evaluate |
| `HAL/X64/Drivers/XHCI/XhciDeviceSetup.c` | PR-S-xhcidevice-1：Set* / SetupHidDevice |
| `HAL/X64/Drivers/XHCI/XhciDiag.c` | PR-H-xhci-split-2 / PR-S-xhcidag-1：诊断日志核心 |
| `HAL/X64/Drivers/XHCI/XhciDiagFormat.c` | PR-S-xhcidag-1：PHOTO 统计串 / arms 日志 |
| `HAL/X64/Drivers/XHCI/XhciEnum.c` | PR-S-xhci-1：根口枚举键盘并绑鼠标 |
| `HAL/X64/Drivers/XHCI/XhciEvent.c` | PR-H-xhci-msc-split-2：事件环 ProcessEvents* |
| `HAL/X64/Drivers/XHCI/XhciFtdiClaim.c` | PR-H-usb-uart-ftdi-1：FT232 认领（VID/PID + Bulk OUT + 115200） |
| `HAL/X64/Drivers/XHCI/XhciFtdiHub.c` | PR-H-usb-uart-ftdi-1：已有 HID hub 时扫子口认 FT232 |
| `HAL/X64/Drivers/XHCI/XhciFtdiRx.c` | PR-H-usb-uart-ftdi-2：Bulk IN + 剥 2 字节状态 → 字符环 |
| `HAL/X64/Drivers/XHCI/XhciFtdiTx.c` | PR-H-usb-uart：FT232 Bulk OUT TX + 事件匹配（IN/OUT） |
| `HAL/X64/Drivers/XHCI/XhciHidGetReport.c` | PR-S-xhcihid-1：GET_REPORT / 键盘 EP0 兜底 |
| `HAL/X64/Drivers/XHCI/XhciHidIntrKbd.c` | PR-S-xhcihid-1：键盘中断端点配置 / 出队 / 入队 |
| `HAL/X64/Drivers/XHCI/XhciHidIntrMouse.c` | PR-S-xhcihid-1：鼠标中断端点配置 / 入队 |
| `HAL/X64/Drivers/XHCI/XhciHidMouseClaim.c` | PR-S-xhcihid-1：复合设备鼠标认领 |
| `HAL/X64/Drivers/XHCI/XhciHidParse.c` | PR-S-xhcihid-1：配置描述符解析（键/鼠） |
| `HAL/X64/Drivers/XHCI/XhciHub.c` | PR-S-xhcihub-1：Hub 控制与根口认领 |
| `HAL/X64/Drivers/XHCI/XhciHubEnumKbd.c` | PR-S-xhcihub-1：hub 子口键盘枚举 |
| `HAL/X64/Drivers/XHCI/XhciHubEnumMouse.c` | PR-S-xhcihub-1：hub 子口鼠标枚举 |
| `HAL/X64/Drivers/XHCI/XhciHubEnumMsc.c` | PR-S-xhcihub-1：hub 子口 MSC 枚举与第二 hub |
| `HAL/X64/Drivers/XHCI/XhciInitHw.c` | PR-S-xhci-1：BAR/DMAR/Halt/Start/端口勘察 |
| `HAL/X64/Drivers/XHCI/XhciInternal.h` | PR-H-xhci-split-1：xHCI 内部共享（宏/类型/extern/共享声明） |
| `HAL/X64/Drivers/XHCI/XhciIrq.c` | PR-H-xhci-split-8：IRQ / Drain / dual-poll 切换 |
| `HAL/X64/Drivers/XHCI/XhciKeyboard.c` | PR-H-xhci-core-split-6：KbdPush / SetLeds / DequeueKeyboard |
| `HAL/X64/Drivers/XHCI/XhciMmio.c` | PR-H-xhci-core-split-1：MMIO / 内存 / Stall / Wait / MapDma |
| `HAL/X64/Drivers/XHCI/XhciMouse.c` | PR-S-xhcimouse-1：延迟绑定、报告队列与对外 API |
| `HAL/X64/Drivers/XHCI/XhciMouseComposite.c` | PR-S-xhcimouse-1：复合设备同 slot 绑鼠标 |
| `HAL/X64/Drivers/XHCI/XhciMousePort.c` | PR-S-xhcimouse-1：其它根口绑鼠标 |
| `HAL/X64/Drivers/XHCI/XhciMsc.c` | PR-S-xhcimsc-1：MSC 核心（BringUp / Ready / Scan / 容量查询入口） |
| `HAL/X64/Drivers/XHCI/XhciMscBot.c` | PR-S-xhcimsc-1：从 XhciMsc.c 原样搬家；不改语义。 |
| `HAL/X64/Drivers/XHCI/XhciMscBulk.c` | PR-S-xhcimsc-1：从 XhciMsc.c 原样搬家；不改语义。 |
| `HAL/X64/Drivers/XHCI/XhciMscClaim.c` | MSC FinishClaim / ConfigEP（PR-S3-xhcimscclaim-1） |
| `HAL/X64/Drivers/XHCI/XhciMscClaimParse.c` | MSC 配置描述符解析（PR-S3-xhcimscclaim-1） |
| `HAL/X64/Drivers/XHCI/XhciMscClaimPort.c` | PR-S-xhcimsc-1：单口 Force / Address / 根口 hub。 |
| `HAL/X64/Drivers/XHCI/XhciMscClaimPorts.c` | PR-S-xhcimsc-1：根口/hub claim 编排。 |
| `HAL/X64/Drivers/XHCI/XhciPort.c` | PR-H-xhci-split-3：端口复位 / 上电 / PORTSC |
| `HAL/X64/Drivers/XHCI/XhciRing.c` | PR-H-xhci-core-split-2：TRB 环 / DCBAA / ResolveFwCmdRing |
| `HAL/X64/Drivers/XHCI/XhciTransfer.c` | PR-H-xhci-core-split-4：WaitTransfer / ServiceHidCompletions |
| `HAL/X64/Drivers/XHCI/XhciWifiClaim.c` | xHCI 上认 RTL8188EU（PR-N-wifi-1） |
| `HAL/X64/Drivers/XHCI/XhciWifiHub.c` | 已有 HID hub 时扫子口认 8188EU（PR-N-wifi-1） |

## 11. `HAL/Virt/` — Arm/RiscV 共享 virtio / ramfb / DTB

### (Virt 根)

| 文件 | 用途 |
| ---- | ---- |
| `HAL/Virt/Dtb.c` | FDT memory 节点 reg（PR-S3-dtb-1） |
| `HAL/Virt/Dtb.h` | 最小 FDT 解析（HAL 内；Common 不碰 DTB，PR-V1） |
| `HAL/Virt/DtbPrivate.h` | Dtb / DtbProbe 内部交接（PR-S3-dtb-1） |
| `HAL/Virt/DtbProbe.c` | FDT fw-cfg 基址与 cpu@ 计数（PR-S3-dtb-1） |
| `HAL/Virt/HalVideo.c` | 帧缓冲门面，委托 Drivers/Video（PR-V2 ramfb scanout） |
| `HAL/Virt/HelloBlob.c` | PR-A7：无嵌入 User/hello.elf；满足 ProcessRunDemo 链接符号 */ |
| `HAL/Virt/README.md` | HAL/Virt — Arm64 / RiscV 共享 virt 实现（PR-R5） |
| `HAL/Virt/Ramfb.c` | QEMU fw_cfg MMIO + etc/ramfb（PR-V2） |
| `HAL/Virt/Ramfb.h` | QEMU ramfb via fw_cfg（PR-V2；HAL 内，非 GOP/UEFI） |
| `HAL/Virt/VirtioBlock.c` | virtio-blk MMIO（PR-V4；PR-D2 经 Driver Block 类） |
| `HAL/Virt/VirtioBlock.h` | virtio-blk-device（PR-V4 / PR-D2） |
| `HAL/Virt/VirtioInput.c` | virtio-input MMIO 驱动注册与轮询（PR-S3-virtioinput-1） |
| `HAL/Virt/VirtioInput.h` | virtio-keyboard / tablet（PR-V3 / PR-D3） |
| `HAL/Virt/VirtioInputDiag.c` | PR-V-input-diag：virtio-input 只读计数（不改发送/解析） |
| `HAL/Virt/VirtioInputDiag.h` | PR-V-input-diag：virtio-input 只读计数 |
| `HAL/Virt/VirtioInputEv.c` | evdev 解码 / 队列 / ring drain（PR-S3-virtioinput-1） |
| `HAL/Virt/VirtioInputPrivate.h` | VirtioInput / VirtioInputEv 内部交接（PR-S3-virtioinput-1） |
| `HAL/Virt/VirtioMmio.c` | virtio-mmio（legacy v1 + modern v2）（PR-V3/V4） |
| `HAL/Virt/VirtioMmio.h` | QEMU virt virtio-mmio（modern v2）公共层（PR-V3/V4） |
| `HAL/Virt/VirtioNet.h` | virtio-net-device MMIO（PR-N9 / PR-D3） |

### VirtioNet

| 文件 | 用途 |
| ---- | ---- |
| `HAL/Virt/VirtioNet/VirtioNet.c` | virtio-net MMIO 探测与驱动注册（PR-S-virtionet-virt-1） |
| `HAL/Virt/VirtioNet/VirtioNetAddr.c` | IP 文本格式（PR-S-virtionet-virt-1） |
| `HAL/Virt/VirtioNet/VirtioNetArp.c` | ARP/ICMP 与帧发送（PR-S-virtionet-virt-1） |
| `HAL/Virt/VirtioNet/VirtioNetPrivate.h` | MMIO virtio-net 状态（PR-S-virtionet-virt-1） |
| `HAL/Virt/VirtioNet/VirtioNetProtocol.c` | SendIp / Ping（PR-S-virtionet-virt-1） |
| `HAL/Virt/VirtioNet/VirtioNetRx.c` | 接收环与入站分发（PR-S-virtionet-virt-1） |

## 12. `HAL/Arm64/` — AArch64 HAL + Board/virt

### (Arch 核心)

| 文件 | 用途 |
| ---- | ---- |
| `HAL/Arm64/DeviceEnum.c` | Arm64 设备枚举（占位；PR-DEV-5 可选 DTB） |
| `HAL/Arm64/Exception.c` | PR-A10 缺页 + PR-A11 用户 SVC / 自测 |
| `HAL/Arm64/Gic.c` | PR-A13/A14：QEMU virt GICv2（distributor + CPU interface） |
| `HAL/Arm64/Hal/Hal.c` | PR-S3-hal-arm-1：CPU / IRQ / Timer / 用户地址布局 |
| `HAL/Arm64/Hal/HalConsole.c` | 控制台门面（串口 + 帧缓冲文字；PR-V5/V6 桌面） |
| `HAL/Arm64/Hal/HalDevices.c` | Arm64：注册 / Block·USB 桩 / Wifi·Iwl / Igpu·Hda（PR-S3-haldev-1） |
| `HAL/Arm64/Hal/HalDevicesInput.c` | Arm64：Input / EHCI·UHCI·PS2 桩（PR-S3-haldev-1） |
| `HAL/Arm64/Hal/HalDevicesNet.c` | Arm64：Net 门面（PR-S3-haldev-1） |
| `HAL/Arm64/Hal/HalFrame.c` | PR-S3-hal-arm-1：中断帧 / TLS / 调度进入 |
| `HAL/Arm64/Hal/HalPlat.c` | PR-S3-hal-arm-1：架构名 / ELF / 调试 / 平台探测 |
| `HAL/Arm64/Hal/HalPort.h` | PR-A15：中立名（ELR_EL1 / SPSR_EL1 / SP_EL0 等由 Vectors.S 填） */ |
| `HAL/Arm64/Hal/HalSerial.c` | QEMU virt aarch64 PL011 UART（基址见 BoardConfig.h / PR-B2） |
| `HAL/Arm64/Io.c` | 无传统端口 I/O；占位实现 |
| `HAL/Arm64/PageTable.c` | HAL/Arm64 页表 Hal* API（PR-S3-pagetable-1） |
| `HAL/Arm64/PageTablePrivate.h` | HAL/Arm64 PageTableWalk / PageTable 内部交接（PR-S3-pagetable-1） |
| `HAL/Arm64/PageTableWalk.c` | HAL/Arm64 页表遍历与辅助（PR-S3-pagetable-1） |
| `HAL/Arm64/Platform.c` | Arm64 平台 MMIO 占位 |
| `HAL/Arm64/README.md` | ARM64 HAL |
| `HAL/Arm64/Smp.c` | PR-A14：QEMU virt aarch64 PSCI CPU_ON + 每核 idle |
| `HAL/Arm64/SmpEntry.S` | PR-A14：PSCI CPU_ON 入口（x0=logical cpu） |
| `HAL/Arm64/Startup.S` | QEMU virt aarch64 -kernel 入口（PR-V1：保留 x0=DTB） |
| `HAL/Arm64/Startup.c` | QEMU virt aarch64：DTB → BOOT_INFO（PR-V1）/ ramfb（PR-V2） |
| `HAL/Arm64/Vectors.S` | PR-A10 缺页 + PR-A11 Lower-EL SVC / 用户帧 |
| `HAL/Arm64/link.ld` | link.ld — QEMU virt aarch64（-kernel ELF） |

### Board/virt

| 文件 | 用途 |
| ---- | ---- |
| `HAL/Arm64/Board/virt/Board.c` | QEMU aarch64 virt 板包骨架（PR-B2） |
| `HAL/Arm64/Board/virt/Board.h` | QEMU aarch64 virt 板包门面（PR-B2） |
| `HAL/Arm64/Board/virt/BoardConfig.h` | QEMU aarch64 virt（PR-B2） |
| `HAL/Arm64/Board/virt/README.md` | Board：`virt`（QEMU aarch64 virt） |

### Drivers

| 文件 | 用途 |
| ---- | ---- |
| `HAL/Arm64/Drivers/.gitkeep` | （见文件名 / 同目录 README） |

## 13. `HAL/RiscV/` — RISC-V HAL + Board/virt

### (Arch 核心)

| 文件 | 用途 |
| ---- | ---- |
| `HAL/RiscV/DeviceEnum.c` | RiscV 设备枚举（占位；PR-DEV-5 可选 DTB） |
| `HAL/RiscV/Hal/Hal.c` | PR-S3-hal-riscv-1：CPU / IRQ / Timer / 用户地址布局 |
| `HAL/RiscV/Hal/HalConsole.c` | 控制台门面（串口 + 帧缓冲文字；PR-V5/V6 桌面） |
| `HAL/RiscV/Hal/HalDevices.c` | RiscV：注册 / Block·USB 桩 / Wifi·Iwl / Igpu·Hda（PR-S3-haldev-1） |
| `HAL/RiscV/Hal/HalDevicesInput.c` | RiscV：Input / EHCI·UHCI·PS2 桩（PR-S3-haldev-1） |
| `HAL/RiscV/Hal/HalDevicesNet.c` | RiscV：Net 门面（PR-S3-haldev-1） |
| `HAL/RiscV/Hal/HalFrame.c` | PR-S3-hal-riscv-1：中断帧 / TLS / 调度进入 |
| `HAL/RiscV/Hal/HalPlat.c` | PR-S3-hal-riscv-1：架构名 / ELF / 调试 / 平台探测 |
| `HAL/RiscV/Hal/HalPort.h` | PR-A15：中立名（sepc / sstatus / 用户 sp 由 TrapVec.S 填） */ |
| `HAL/RiscV/Hal/HalSerial.c` | RISC-V UART16550 / DW-APB（基址与间距见 BoardConfig.h） |
| `HAL/RiscV/Io.c` | 无传统端口 I/O；占位实现 |
| `HAL/RiscV/PageTable.c` | HAL/RiscV 页表 Hal* API（PR-S3-pagetable-1） |
| `HAL/RiscV/PageTablePrivate.h` | HAL/RiscV PageTableWalk / PageTable 内部交接（PR-S3-pagetable-1） |
| `HAL/RiscV/PageTableWalk.c` | HAL/RiscV 页表遍历与辅助（PR-S3-pagetable-1） |
| `HAL/RiscV/Platform.c` | RISC-V 平台 MMIO 占位 |
| `HAL/RiscV/README.md` | RISC-V HAL |
| `HAL/RiscV/Smp.c` | PR-A14：QEMU virt riscv64 SBI HSM + 每核 idle |
| `HAL/RiscV/SmpEntry.S` | PR-A14：SBI HSM hart_start 入口（a0=hartid a1=opaque=logical） |
| `HAL/RiscV/Startup.S` | QEMU virt riscv64 入口（PR-V1/A14：a0=hartid a1=DTB；OpenSBI） |
| `HAL/RiscV/Startup.c` | RISC-V：DTB / 板级表 → BOOT_INFO（PR-V1）/ ramfb（virt）/ Duo S（PR-B3） |
| `HAL/RiscV/Trap.c` | PR-A10 缺页 + PR-A11 U-mode ecall / 自测 |
| `HAL/RiscV/TrapVec.S` | PR-A10 缺页 + PR-A11 U-mode ecall |
| `HAL/RiscV/link.ld` | link.ld — QEMU virt riscv64（OpenSBI payload @ 0x80200000，PR-V1） |

### Board/milk-v-duo-s

| 文件 | 用途 |
| ---- | ---- |
| `HAL/RiscV/Board/milk-v-duo-s/Board.c` | Milk-V Duo S 板包（PR-B3） |
| `HAL/RiscV/Board/milk-v-duo-s/Board.h` | Milk-V Duo S 板包门面（PR-B3） |
| `HAL/RiscV/Board/milk-v-duo-s/BoardConfig.h` | Milk-V Duo S（SG2000 / CV181x，PR-B3） |
| `HAL/RiscV/Board/milk-v-duo-s/NOTES.md` | Milk-V Duo S 笔记（PR-B3） |
| `HAL/RiscV/Board/milk-v-duo-s/README.md` | Board：`milk-v-duo-s`（Milk-V Duo S / SG2000） |

### Board/virt

| 文件 | 用途 |
| ---- | ---- |
| `HAL/RiscV/Board/virt/Board.c` | QEMU riscv64 virt 板包骨架（PR-B2） |
| `HAL/RiscV/Board/virt/Board.h` | QEMU riscv64 virt 板包门面（PR-B2） |
| `HAL/RiscV/Board/virt/BoardConfig.h` | QEMU riscv64 virt（PR-B2；B3 补齐 RAM / IS_VIRT / REG_SHIFT） |
| `HAL/RiscV/Board/virt/README.md` | Board：`virt`（QEMU riscv64 virt） |

### Drivers

| 文件 | 用途 |
| ---- | ---- |
| `HAL/RiscV/Drivers/.gitkeep` | （见文件名 / 同目录 README） |

## 14. `User/` — 用户态 CRT / 头 / libToy* / Apps / Pkg

### Apps

| 文件 | 用途 |
| ---- | ---- |
| `User/Apps/BlitDemo.c` | PR-G-desk-3：ToyGfxDamageRect 像素 blit 演示 |
| `User/Apps/BrkDemo.c` | PR-P3：malloc 超过旧 BSS 8KiB 上限仍成功 |
| `User/Apps/Cat.c` | CRT2：open/read/write/close 读 TOYOS.ID（CAT.ELF） |
| `User/Apps/CatFile.S` | 旧 int 0x80 对照；CRT 版见 Cat.c（CAT.ELF） */ |
| `User/Apps/Count.S` | 打印 0-9 每行一个数字，验证第二个 ELF 程序 */ |
| `User/Apps/CwdDemo.c` | getcwd / chdir / wait 退出码 |
| `User/Apps/DirDemo.c` | PR-F4：OpenDirectory / ReadDirectory / FileStat 冒烟 |
| `User/Apps/DynDemo.S` | 动态链接演示：调用 LIBTOY.SO 中的 toy_hello */ |
| `User/Apps/EnosysDemo.c` | 开课前：未知 syscall 须返回 -ENOSYS（38） |
| `User/Apps/ExecDemo.c` | PR-P1：用户态 execve 再加载 HELLO.ELF |
| `User/Apps/Fork.S` | fork + 阻塞 wait：父打印 P，子打印 C，父 wait 后再 done */ |
| `User/Apps/GuiDemo.c` | 链 libToyUi/libToyGfx（课堂对照：Documents/技术手册.md） |
| `User/Apps/Hello.c` | CRT：C + printf/malloc（HELLO.ELF） |
| `User/Apps/HelloInt80.S` | 旧版 Ring3：int 0x80 write + exit（教学对照；CRT 见 Hello.c） */ |
| `User/Apps/KillDemo.c` | PR-P4：fork 后父进程 kill(SIGTERM)，再 wait 收尸 |
| `User/Apps/LibToy.S` | 共享库：导出 toy_hello，打印 from.so */ |
| `User/Apps/LibcDemo.c` | PR-L2：atoi / qsort / snprintf / signal 冒烟 |
| `User/Apps/MmapDemo.c` | PR-U-mmap 匿名 + PR-U-mmap2 文件私有映射 |
| `User/Apps/NetDemo.S` | socket/connect/write/read/close via syscalls (needs LWIP=1) */ |
| `User/Apps/NetLibDemo.c` | PR-L4：用 libToyNet 重写 NETDEMO 客户端路径 |
| `User/Apps/NetServer.S` | bind/listen/accept echo once (needs LWIP=1, hostfwd :9000) |
| `User/Apps/PipeDemo.c` | PR-P2：pipe + fork；子写父读 |
| `User/Apps/PthreadSmoke.c` | thr-4：pthread_create/join + 自旋 mutex 冒烟 |
| `User/Apps/SigDemo.c` | PR-U-sig：子进程安装 SIGTERM handler，父 kill 后 handler 运行并退出 |
| `User/Apps/SleepDemo.c` | sleep / clock_ms 稳节拍冒烟 |
| `User/Apps/Snake.c` | 教学贪吃蛇（方向键或 WASD；节拍用 msleep） |
| `User/Apps/SockDemo.c` | 刀 C：POSIX connect(sockaddr) 冒烟（同 NETLIB 路径） |
| `User/Apps/SysFork.S` | fork + wait 走 syscall（对照 FORK.ELF 的 int 0x80） */ |
| `User/Apps/SysHello.S` | Ring 3：用 syscall/sysret 路径 write + exit（对照 HELLO.ELF 的 int 0x80） */ |
| `User/Apps/TaskMgr.c` | 任务管理器（课堂范例 GUI App） |
| `User/Apps/ThreadDemo.c` | thr-5：§0.1 三条故事 + 多线程 fork 失败 / 单线程 fork 回归 |
| `User/Apps/ThreadSmoke.c` | thr-3：create / join / gettid 冒烟 |
| `User/Apps/WaitNoHang.S` | fork + WNOHANG：父轮询 wait/yield，子退出后再 done */ |
| `User/Apps/WinDemo.c` | PR-G14：create_window / damage / poll_input 用户窗演示 |
| `User/Apps/WriteFile.S` | 旧 int 0x80 对照；CRT 版见 WriteFile.c（WRITE.ELF） */ |
| `User/Apps/WriteFile.c` | CRT2：写 USERNOTE.TXT（WRITE.ELF） |

### crt

| 文件 | 用途 |
| ---- | ---- |
| `User/crt/crt0.S` | _start → main(argc,argv) → exit（syscall） |
| `User/crt/crt0_aarch64.S` | PR-A12：_start → main → exit(SVC) |
| `User/crt/crt0_riscv.S` | PR-A12：_start → main → exit(ecall) |
| `User/crt/cwd.c` | getcwd / chdir（SYS_GETCWD / SYS_CHDIR） |
| `User/crt/dirent.c` | OpenDirectory / ReadDirectory / FileStat + readdir（刀 B） |
| `User/crt/errno.c` | 每进程一份；单任务用户程序即可 */ |
| `User/crt/malloc.c` | PR-P3：经 SYS_BRK 扩展堆；PR-A-libc：块头记录 size 供 realloc |
| `User/crt/printf.c` | printf / sprintf / snprintf（%s %d %u %x %c %%）（PR-L2） |
| `User/crt/proc.c` | getpid / getppid（PR-U-getpid；SYS_GETPID / SYS_GETPPID） |
| `User/crt/pthread.c` | thr-4：pthread_* 薄封装 + 自旋 mutex |
| `User/crt/sched.c` | sched_yield（PR-U-sched-yield） |
| `User/crt/signal.c` | PR-U-sig：signal() → SYS_SIGNAL（教学级用户 handler） |
| `User/crt/sleep.c` | sleep / usleep / msleep / clock_ms（SYS_SLEEP / SYS_CLOCK_MS） |
| `User/crt/socket.c` | POSIX connect/bind + 字节序（刀 C） |
| `User/crt/stat.c` | POSIX 形参 stat / fstat（PR-U-stat） |
| `User/crt/stdio.c` | PR-A-libc：无缓冲 fopen / fread / fwrite / fseek |
| `User/crt/stdlib.c` | atoi / qsort / abs（PR-L2）；exit（自 malloc.c 迁入）；malloc 仍在 malloc.c |
| `User/crt/string.c` | （见文件名 / 同目录 README） |
| `User/crt/syscall.S` | long toy_syscall(n, a, b, c) via syscall */ |
| `User/crt/syscall_aarch64.S` | long toy_syscall(n,a,b,c) via SVC */ |
| `User/crt/syscall_riscv.S` | long toy_syscall(n,a,b,c) via ecall */ |
| `User/crt/thread_root.c` | CRT 入口桩：Start(Arg) 后 thread_exit |
| `User/crt/unistd.c` | open/read/write/close，失败置 errno（PR-CRT2） |

### include

| 文件 | 用途 |
| ---- | ---- |
| `User/include/FsUtil.h` | 用户态路径 / 目录 / 多卷薄封装（libFsUtil，PR-A-fsutil） |
| `User/include/ToyGfx.h` | 用户态绘图薄库（libToyGfx） |
| `User/include/ToyNet.h` | 用户态 libToyNet（PR-L4 / 网络双轨 C1 / 刀 C） |
| `User/include/ToySyscall.h` | 兼容入口（PR-L1） |
| `User/include/ToyUi.h` | 用户态控件薄库（libToyUi） |
| `User/include/ctype.h` | 最小字符分类（PR-L2） |
| `User/include/dirent.h` | 目录 / 文件状态（PR-F4 + PR-U-abi-dual 刀 B） |
| `User/include/errno.h` | 用户态错误码（PR-CRT2 / PR-N-dns） |
| `User/include/fcntl.h` | open 与标志（PR-CRT2；内核暂忽略 flags） |
| `User/include/pthread.h` | ToyOS 教学子集（thr-4）：create/join/exit + 自旋 mutex |
| `User/include/sched.h` | POSIX 调度（PR-U-sched-yield） |
| `User/include/signal.h` | PR-P4 + PR-U-sig：kill / signal（教学子集，无 sigaction） |
| `User/include/stdarg.h` | 最小 va_list（PR-L2；用编译器 builtin） |
| `User/include/stddef.h` | 最小 freestanding 类型（PR-L1） |
| `User/include/stdio.h` | PR-A-libc：无缓冲 FILE*；fopen("w") 不截断（内核忽略 O_TRUNC） */ |
| `User/include/stdlib.h` | （见文件名 / 同目录 README） |
| `User/include/string.h` | （见文件名 / 同目录 README） |
| `User/include/sys/mman.h` | mmap / munmap（PR-U-mmap 匿名；PR-U-mmap2 文件私有） |
| `User/include/sys/socket.h` | 第 1 轨 POSIX 套接字声明（PR-U-abi-dual 刀 C） |
| `User/include/sys/stat.h` | 教学最小 stat / fstat（PR-U-stat） |
| `User/include/sys/types.h` | 用户态基础类型（PR-L1 + 刀 C socklen） |
| `User/include/toyos.h` | 用户态 CRT 伞头（PR-L1） |
| `User/include/toyos/syscall.h` | 系统调用号与薄封装（PR-L1；原 toy_syscall.h） |
| `User/include/toyos/task.h` | 任务快照（SYS_TASK_SNAP；与 Shell `ps` 同源字段） |
| `User/include/toyos/thread.h` | PR-U-thread：入口桩 + create/join/exit/gettid |
| `User/include/toyos/version.h` | CRT / libtoyos 版本（PR-L1） |
| `User/include/unistd.h` | read/write/close/execve/pipe/dup/fork/wait/brk/kill + 窗口 + sleep |

### Library

| 文件 | 用途 |
| ---- | ---- |
| `User/Library/FsUtil/FsUtil.c` | libFsUtil（PR-A-fsutil） |
| `User/Library/ToyGfx/ToyGfx.c` | libToyGfx |
| `User/Library/ToyNet/ToyNet.c` | libToyNet（PR-L4 / PR-N-dns / PR-A-net-dns） |
| `User/Library/ToyUi/ToyUi.c` | libToyUi 核心：窗 / 标签 / 底栏按钮 / Poll（PR-S-toyui-1） |
| `User/Library/ToyUi/ToyUiPrivate.h` | PR-S-toyui-1：libToyUi 模块内共享 |
| `User/Library/ToyUi/ToyUiWidgets.c` | PR-S-toyui-1：复选框 / 列表 / 输入框 |

### Pkg

| 文件 | 用途 |
| ---- | ---- |
| `User/Pkg/Gui/Makefile` | （见文件名 / 同目录 README） |
| `User/Pkg/Gui/main.c` | Pkg/Gui — 第一个 GUI 程序模板（PR-L3） |
| `User/Pkg/Makefile` | （见文件名 / 同目录 README） |
| `User/Pkg/Net/Makefile` | （见文件名 / 同目录 README） |
| `User/Pkg/Net/main.c` | Pkg/Net — libToyNet 课外模板（PR-L4） |
| `User/Pkg/README.md` | 用户程序模板（PR-L1） |
| `User/Pkg/ToyUser.mk` | ToyUser.mk — 课外 / 模板共用规则（PR-L1） |
| `User/Pkg/main.c` | 课外用户程序模板示例（PR-L1） |

### user-arm64.ld

| 文件 | 用途 |
| ---- | ---- |
| `User/user-arm64.ld` | PR-A12：Arm64 用户 ELF @ HalUserCodeVirt (4GiB) */ |

### user-riscv.ld

| 文件 | 用途 |
| ---- | ---- |
| `User/user-riscv.ld` | PR-A12：RiscV 用户 ELF @ HalUserCodeVirt (4GiB) */ |

### user.ld

| 文件 | 用途 |
| ---- | ---- |
| `User/user.ld` | PF_R\|PF_X */ |

## 15. `Assets/` — Guest 资源种子（同步到 ToyImage RootFs）

### Fonts

| 文件 | 用途 |
| ---- | ---- |
| `Assets/Fonts/README.md` | .FNT`。启动时（FS 就绪后）`FontLoadAssets`： |

### Icons

| 文件 | 用途 |
| ---- | ---- |
| `Assets/Icons/LICENSE-Lucide.txt` | （见文件名 / 同目录 README） |
| `Assets/Icons/README.md` | ToyOS 桌面 / UI 图标 |

### Locale

| 文件 | 用途 |
| ---- | ---- |
| `Assets/Locale/README.md` | Assets/Locale — 可编辑 UI 文案 |
| `Assets/Locale/en.txt` | （见文件名 / 同目录 README） |
| `Assets/Locale/zh.txt` | （见文件名 / 同目录 README） |

### Packs

| 文件 | 用途 |
| ---- | ---- |
| `Assets/Packs/README.md` | Assets/Packs — 已安装资源包（PR-S3） |

### Store

| 文件 | 用途 |
| ---- | ---- |
| `Assets/Store/HANDTEST-MOD.md` | .ELF` 扁平 \| |
| `Assets/Store/README.md` | .ELF`） \| |
| `Assets/Store/ROOTFS-ELF.md` | .ELF`（不含 `Kernel.elf`）；另记 `LIBTOY.SO` 与扁平 `Apps/*.ELF`。 |
| `Assets/Store/catalog.txt` | （见文件名 / 同目录 README） |
| `Assets/Store/packages/blitdemo/PKG.TXT` | （见文件名 / 同目录 README） |
| `Assets/Store/packages/cat/PKG.TXT` | （见文件名 / 同目录 README） |
| `Assets/Store/packages/demopack/INFO.TXT` | （见文件名 / 同目录 README） |
| `Assets/Store/packages/demopack/PKG.TXT` | （见文件名 / 同目录 README） |
| `Assets/Store/packages/guidemo/Assets/INFO.TXT` | （见文件名 / 同目录 README） |
| `Assets/Store/packages/guidemo/PKG.TXT` | （见文件名 / 同目录 README） |
| `Assets/Store/packages/hello/PKG.TXT` | （见文件名 / 同目录 README） |
| `Assets/Store/packages/snake/PKG.TXT` | （见文件名 / 同目录 README） |
| `Assets/Store/packages/sun8/PKG.TXT` | （见文件名 / 同目录 README） |
| `Assets/Store/packages/taskmgr/PKG.TXT` | （见文件名 / 同目录 README） |
| `Assets/Store/packages/windemo/PKG.TXT` | （见文件名 / 同目录 README） |

## 16. `Tools/` — SDK / 脚本 / Host 单测 / 交叉工具链

### (Tools 根)

| 文件 | 用途 |
| ---- | ---- |
| `Tools/README.md` | ToyKernel/Tools |
| `Tools/build-sdk.sh` | !/bin/bash → `Build/ToySDK` |

### .gitignore

| 文件 | 用途 |
| ---- | ---- |
| `Tools/.gitignore` | （见文件名 / 同目录 README） |

### Scripts

| 文件 | 用途 |
| ---- | ---- |
| `Tools/Scripts/pack-app.sh` | 把已编 ELF 填进 Assets/Store/packages/ |
| `Tools/Scripts/runtests.sh` | Host 单测入口：scheduler / memory / fs |
| `Tools/Scripts/test-all.sh` | 开课前统一验收（旁路 ToyImage） |

### Tests

| 文件 | 用途 |
| ---- | ---- |
| `Tools/Tests/TestFs.c` | Vfs 契约 Host 单测。 |
| `Tools/Tests/TestMemory.c` | 对 bitmap 政策断言。宿主 gcc，不链内核。 |
| `Tools/Tests/TestScheduler.c` | 对 round-robin 政策断言。宿主 gcc 编译，不链内核。 |
| `Tools/Tests/Stub/Debug.h` | Host 单测空桩（不拉 Hal.h）。 |
| `Tools/Tests/Stub/FsStub.c` | Host 契约测用 Synthetic 后端（单文件 HI.TXT）。 |
| `Tools/Tests/Stub/MemoryStub.c` | 假段表：段 0 有 63 页（BasePhys=PAGE_SIZE，避开 NULL）。 |
| `Tools/Tests/Stub/PhysicalMemoryPrivate.h` | Host 单测桩（-I Tools/Tests/Stub 优先于 Include/）。 |
| `Tools/Tests/Stub/SchedHostTypes.h` | Host 单测里的瘦任务。只保留政策会碰的字段。 |
| `Tools/Tests/Stub/SchedulerPrivate.h` | Host 桩声明。真实框架符号见 Include/SchedulerPrivate.h。 |
| `Tools/Tests/Stub/SchedulerStub.c` | 队列、空闲任务、CPU 数。不含锁和 steal。 |

### Sdk

| 文件 | 用途 |
| ---- | ---- |
| `Tools/Sdk/Examples/Blit/Makefile` | （见文件名 / 同目录 README） |
| `Tools/Sdk/Examples/Blit/main.c` | Examples/Blit — ToyGfxDamageRect + 点/线/矩形封装（PR-A-gfx-api） |
| `Tools/Sdk/Examples/Dir/Makefile` | （见文件名 / 同目录 README） |
| `Tools/Sdk/Examples/Dir/main.c` | Examples/Dir — OpenDirectory / ReadDirectory（PR-A-examples） |
| `Tools/Sdk/Examples/File/Makefile` | （见文件名 / 同目录 README） |
| `Tools/Sdk/Examples/File/main.c` | Examples/File — open / write / read + fopen / fseek（PR-A-libc） |
| `Tools/Sdk/Examples/Fork/Makefile` | （见文件名 / 同目录 README） |
| `Tools/Sdk/Examples/Fork/main.c` | Examples/Fork — fork + wait（PR-A-examples） |
| `Tools/Sdk/Examples/Fs/Makefile` | （见文件名 / 同目录 README） |
| `Tools/Sdk/Examples/Fs/main.c` | Examples/Fs — libFsUtil 路径拼接 / 列目录 / TOYOS: 前缀（PR-A-fsutil） |
| `Tools/Sdk/Examples/Gui/Makefile` | （见文件名 / 同目录 README） |
| `Tools/Sdk/Examples/Gui/main.c` | Examples/Gui — 标签/按钮 + 复选框/列表/输入框（PR-A-ui-api） |
| `Tools/Sdk/Examples/Hello/Makefile` | （见文件名 / 同目录 README） |
| `Tools/Sdk/Examples/Hello/main.c` | Examples/Hello — CRT 最小示例（PR-A-examples） |
| `Tools/Sdk/Examples/Net/Makefile` | （见文件名 / 同目录 README） |
| `Tools/Sdk/Examples/Net/main.c` | Examples/Net — libToyNet TCP 客户端（PR-A-net-dns） |
| `Tools/Sdk/Examples/Pipe/Makefile` | （见文件名 / 同目录 README） |
| `Tools/Sdk/Examples/Pipe/main.c` | Examples/Pipe — pipe + fork；子写父读（PR-A-examples） |
| `Tools/Sdk/Examples/README.md` | ToyOS SDK 示例 |
| `Tools/Sdk/Makefile.template` | （见文件名 / 同目录 README） |
| `Tools/Sdk/README.md` | ToyOS SDK |
| `Tools/Sdk/ToySdk.mk` | ToyOS SDK 应用规则（PR-A-sdk-pack） |
| `Tools/Sdk/VERSION` | （见文件名 / 同目录 README） |

## 17. `ThirdParty/` — 第三方集成说明

| 文件 | 用途 |
| ---- | ---- |
| `ThirdParty/README.md` | ThirdParty |

## 18. `Documents/` — 文档

### README.md

| 文件 | 用途 |
| ---- | ---- |
| `Documents/README.md` | ToyKernel / Documents — 文档总索引 |

### 已完

| 文件 | 用途 |
| ---- | ---- |
| `Documents/已完/Console拆分.md` | 任务：拆分 Console.c（PR-S-console-split-1 / -2） |
| `Documents/已完/Desktop拆分.md` | 任务：拆分 Desktop.c（PR-S-desktop-split-1 / -2 / -3） |
| `Documents/已完/FilesUi拆分.md` | .c`，**不必改**。 |
| `Documents/已完/GUI一致性与后台任务.md` | ToyOS GUI 一致性 + 按钮事件 + 后台任务模型 |
| `Documents/已完/GUI美化规划.md` | ToyOS GUI 美化规划 |
| `Documents/已完/GitHub-CI规划.md` | .yml`；不部署服务器；单 job `timeout-minutes` ≤ 30。 |
| `Documents/已完/GuiCompose拆分.md` | 任务：拆分 GuiCompose.c（PR-S-compose-split-1） |
| `Documents/已完/GuiWm拆分.md` | 任务：拆分 GuiWm.c（PR-S-guiwm-split-1 / -2） |
| `Documents/已完/I219真机网课路径.md` | NUC I219 真机网 · 课路径（PR-N-i219） |
| `Documents/已完/Net拆分.md` | 任务：拆分 Net.c（PR-H-net-split-1） |
| `Documents/已完/PMM稀疏化.md` | ToyOS PMM 稀疏化（内存上限 256GB） |
| `Documents/已完/README.md` | Documents/已完 — 已完成归档 |
| `Documents/已完/ShellFs拆分.md` | 任务：拆分 ShellCommandsFs.c（PR-S-shellfs-split-1） |
| `Documents/已完/Video拆分.md` | 任务：拆分 Video.c（PR-H-video-split-1 / -2） |
| `Documents/已完/XHCI事件环单消费者.md` | 任务：xHCI 事件环单消费者（PR-H-xhci-evt-excl） |
| `Documents/已完/XHCI拆分.md` | 任务：把 HAL/X64/Drivers/XHCI.c 拆分为 8 个文件 + 1 个内部头文件 |
| `Documents/已完/XHCI拆分2.md` | .c` \| **不必改**；新 `.c` 自动编入 \| |
| `Documents/已完/home-xhci-handoff.md` | 家 ↔ 公司：真机 xHCI 交接（给 Cursor / 人） |
| `Documents/已完/real-pc-usb-pr-split.md` | 真机 USB 键盘：小 PR 切分（Real-PC USB PR split） |
| `Documents/已完/今日USB键盘逻辑对照-开盘vs当前.md` | 今日 USB 键盘逻辑对照：开盘基线 vs 当前（kbd-v6） |
| `Documents/已完/协作历程日志.md` | ToyOS 协作历程日志 |
| `Documents/已完/双机交接-PR-N-nic.md` | 双机交接：PR-N-nic（近 12 小时） |
| `Documents/已完/可替换模块化架构规划.md` | ToyOS 可替换模块化架构规划 |
| `Documents/已完/大文件拆分.md` | .c`（split-1…8 ✅） \| |
| `Documents/已完/大文件拆分3.md` | .h/*.S` + Makefile/脚本 **>300** \| **74** \| 含资源与文档旁脚本 \| |
| `Documents/已完/大文件拆分续.md` | .c`，**不进子目录**。某个模块第一次开目录时，同一刀补上该目录的 wildcard。这只是把新文件编进来。 |
| `Documents/已完/应用开发生态规划.md` | ToyOS 应用开发生态规划 |
| `Documents/已完/应用资源自包含与字体共享.md` | ToyOS 应用资源自包含（方案 B）+ 字体共享（方案 C） |
| `Documents/已完/开机日志规划.md` | ToyOS 开机日志规划 |
| `Documents/已完/真机冒烟清单.md` | 真机冒烟清单（PR-PC-smoke） |
| `Documents/已完/真机复合USB键盘排障历程.md` | 真机 USB 键鼠排障历程（NUC 参考） |
| `Documents/已完/科技感主题规划.md` | ToyOS 科技感主题配色（Tech Theme） |
| `Documents/已完/自动化功能测试.md` | ToyOS 自动化功能测试 |
| `Documents/已完/设备管理器增强.md` | ToyOS 设备管理器增强 · 人类可读设备视图 |
| `Documents/已完/设备管理器系统摘要.md` | ToyOS 设备管理器 · GUI 系统摘要（About / Summary） |
| `Documents/已完/设备管理器规划.md` | /PCIe.h`） \| |
| `Documents/已完/设备管理器设备树.md` | ToyOS 设备管理器 · 阶段 3 — 设备树拓扑 |
| `Documents/已完/设备管理器资源管理.md` | ToyOS 设备管理器 · 阶段 4 — 资源管理 |
| `Documents/已完/设备管理器驱动匹配.md` | ToyOS 设备管理器 · 阶段 2 — 驱动匹配表（DRIVER_MATCH） |
| `Documents/已完/驱动框架标准化-PR拆分.md` | .c` 通配自动编入） \| |
| `Documents/已完/驱动框架现状分析.md` | ToyOS 驱动框架现状分析 |
| `Documents/已完/驱动模板设计.md` | /*.c` **禁止**出现在 `DRIVER_SRCS`；硬验收用**精确符号** `nm`（见 §1.2），不得出现模板定义的符号 \| |

### 开发

| 文件 | 用途 |
| ---- | ---- |
| `Documents/开发/ABI-API双轨制.md` | .S` 用 `#include <toyos/syscall.h>` + `SYS_*`（`__ASSEMBLER__` 只暴露宏） \| |
| `Documents/开发/API速查.md` | ToyOS API 速查 |
| `Documents/开发/Intel核显2D-blit.md` | Intel 核显 2D blit（PR-G-igpu · 活文档） |
| `Documents/开发/README.md` | Documents/开发 — 命名与用户态应用 |
| `Documents/开发/声卡驱动-HDA.md` | 声卡驱动 · Intel HDA（PR-G-audio · 活文档） |
| `Documents/开发/如何写一个任务管理器App.md` | 如何写一个任务管理器 App（端到端范例） |
| `Documents/开发/如何写一个分配器.md` | 如何写一个分配器 |
| `Documents/开发/如何写一个文件系统.md` | 如何写一个文件系统 |
| `Documents/开发/如何写一个调度器.md` | 如何写一个调度器 |
| `Documents/开发/学生速查卡.md` | WEXITSTATUS(st)==7 */ |
| `Documents/开发/应用开发指南.md` | ToyOS 应用开发指南 |
| `Documents/开发/开发命名规范.md` | ToyOS 开发命名规范 |
| `Documents/开发/开发者接手指南.md` | ToyOS 开发者接手指南（开课后自学） |
| `Documents/开发/开机流程与加速.md` | 开机流程与加速（活文档） |
| `Documents/开发/开课ABI冻结.md` | 开课 ABI 冻结表（印刷 · 大纲夹页） |
| `Documents/开发/用户态线程.md` | 用户态线程（PR-U-thread · 活文档） |
| `Documents/开发/第三次代码审查.md` | `、`ThirdParty/lwip/**` \| 允许 `sys_now` / `toy_socket.c` 等原项目风格 \| |
| `Documents/开发/网络API双轨化-执行前分析.md` | 网络 API 双轨化 · 执行前分析（C1 已定） |

### 待做

| 文件 | 用途 |
| ---- | ---- |
| `Documents/待做/README.md` | Documents/待做 — 规划中 / 未收官规格 |
| `Documents/待做/开课前接口冻结与教学准备.md` | 开课前接口冻结与教学准备 |
| `Documents/待做/模块化与App课堂闭环.md` | /Drivers/` **根**丢业务实现。 |

### 技术手册.md

| 文件 | 用途 |
| ---- | ---- |
| `Documents/技术手册.md` | ToyOS 技术手册（白皮书） |

### 文档章节索引.md

| 文件 | 用途 |
| ---- | ---- |
| `Documents/文档章节索引.md` | ToyOS 文档章节索引（自动生成） |

### 路线图.md

| 文件 | 用途 |
| ---- | ---- |
| `Documents/路线图.md` | ToyOS 发展路线图 |

### 驱动

| 文件 | 用途 |
| ---- | ---- |
| `Documents/驱动/README.md` | Documents/驱动 — 写驱动 |
| `Documents/驱动/网卡扩展-高通Realtek无线.md` | 网卡扩展规划：高通 Atheros / Realtek / 无线 |
| `Documents/驱动/驱动匹配规范.md` | ToyOS 驱动匹配规范 |
| `Documents/驱动/驱动开发指南.md` | .c` 与已登记子目录（`XHCI/` `E1000/` `Iwl/` `Ehci/` …）由 `Makefile` `wildcard` 编入。**新建** `<Device>/` 时须在 `Makefile` 的 `DRIVER_SRC |
| `Documents/驱动/驱动开发范例-网卡L2.md` | 驱动开发范例：网卡 L2（PR-N-nic） |

## 19. `Meta/` — CI / 编辑器元数据

| 文件 | 用途 |
| ---- | ---- |
| `.cursor/rules/home-xhci.mdc` | （见文件名 / 同目录 README） |
| `.github/workflows/build.yml` | （见文件名 / 同目录 README） |
| `.github/workflows/release.yml` | （见文件名 / 同目录 README） |

## 附 A. 二进制资源

| 位置 | 模块 | 用途 |
| ---- | ---- | ---- |
| `Assets/Fonts/*.FNT` | Assets | 点阵字体；prepare-rootfs 同步 |
| `Assets/Icons/**` | Assets | 桌面/任务栏图标（svg/png/bmp48） |
| `Assets/Images/WALL.BMP` | Assets | 壁纸 |
| `Assets/Sounds/BEEP.WAV` | Assets/Sounds | HDA `play` 样例 |
| `Assets/Store/packages/**/*.ELF` | Assets | 商店包载荷（构建副本） |
| `ThirdParty/lwip/` | ThirdParty | lwIP 上游；见 `ThirdParty/README.md` |
| `Build/` | 产物 | `Kernel.elf`、各 `.o`、**`Build/ToySDK/`** |

## 附 B. 与 ToyBoot / ToyImage 边界

| 仓 / 路径 | 关系 |
| --------- | ---- |
| ToyBoot | 仅 x86：`BOOTX64.EFI`；`BOOT_CONFIG` → `HAL/X64/Startup` → `BOOT_INFO` |
| ToyImage `Esp/X64/` | UEFI 引导分区内容 |
| ToyImage `RootFs/X64/` | TOYOS 系统盘：`build.sh` 同步 Kernel / ELF / Assets |
| ToyImage `RootFs/{Arm64,RiscV}/` | virt staging；FAT 盘 `disk.img`（gitignore） |

---

*维护：目录大变（新 Drivers 子夹 / Services 归夹）后重跑生成或手工改本表；用途列优先取源文件头注释。*
