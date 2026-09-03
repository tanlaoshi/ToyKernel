# ToyKernel

ToyOS 的裸机内核（x86-64 为主）。与 [ToyBoot](../ToyBoot/)（UEFI 引导）和 [ToyImage](../ToyImage/)（QEMU 镜像与启动脚本）配合，构成完整的教学/实验用操作系统。

更细的模块说明见 [`结构说明.md`](结构说明.md)，分层与 API 边界见 [`架构分层.md`](架构分层.md)，发展规划见 [`路线图.md`](路线图.md)。

---

## 当前能力概览

| 方向 | 状态 | 说明 |
|------|------|------|
| 虚拟内存 + Ring 3 | ✅ | 四级页表、用户段、`int 0x80`（legacy）与 `syscall`/`sysret` |
| 进程隔离 | ✅ | 独立地址空间、`exec`、**fork / wait / yield**、简易 `.so` |
| 文件与存储 | ✅ | ATA PIO、GPT、FAT 根目录读/写、双盘挂载 |
| 图形界面 | ≈ 可用 | 多窗口、USB 键鼠、**标题栏拖动** |
| 网络 | 进行中 | virtio-net；builtin TCP/UDP（legacy）+ 可选 lwIP |
| 多核 SMP | ✅ | PR-S1～S4：MADT/SIPI、每核 timer、调度大锁、每核 TSS/队列偷任务 |

---

## 近期更新

### 进程与用户态（阶段 2）

- **fork / wait / yield**：`VirtualMemorySpaceClone`（COW）；子进程 exit 变 zombie；`wait` 默认可阻塞，`rdi=WNOHANG` 非阻塞
- **系统调用扩展**：`open` / `read` / `close` / `fork` / `wait`；每任务 FD 表（打开时整文件读入内核缓冲）
- **用户程序**：`HELLO.ELF`、`COUNT.ELF`、`FORK.ELF`、`CAT.ELF`
- **多任务内核栈修复**：每个用户任务切换时设置独立 TSS `RSP0`（`ArchSetRsp0`），避免子进程 syscall 覆盖父进程中断帧导致 `exec FORK.ELF` 失败

### 存储与启动

- **Block + GPT + FAT**：块设备抽象、分区解析、FAT16/32（8.3 + LFN，`.`/`..`，`mkdir`/`rmdir`，写 ≤1MB）
- **卷选择**：优先挂载含 `TOYOS.ID` 的 FAT 卷
- **双盘 QEMU**：`ToyImage/run-split.sh` — 盘 0 为 Boot/ESP，盘 1 为 `rootfs/`（系统文件唯一来源）

### 图形与输入

- **GUI**：Shell 窗口、焦点、标题栏拖动（`VideoCopyRect` 局部刷新）
- **USB**：xHCI 键盘 + tablet 鼠标

### 网络

- **virtio-net**：ARP、ICMP；默认走自研 builtin 栈（`Net`/`Udp`/`Tcp`，**legacy**）
- **lwIP（可选）**：`./build.sh LWIP=1` 后 `lwip on` → RX/TX 仅 lwIP；同名 Shell 命令自动切换
- **用户态 socket**：`socket`/`connect`/`bind`/`listen`/`accept`；`NETDEMO.ELF` / `NETSRV.ELF`（需 LWIP=1）
- **双栈策略**：运行时一帧一栈、不热切回 builtin；新功能以 lwIP 为准 → 详见 [`ThirdParty/README.md`](ThirdParty/README.md)

### 架构与工程

- **HAL 分层**：端口 I/O 为 `HalIoRead/Write*`；设备驱动在 `HAL/<Arch>/Drivers/`
- **目录重构（PR-1/2）**：`Common/{Core,Services,Library}`、`Include/` 公共头、`HAL/{X86_64,Arm64,RiscV}/Drivers/`
- **Boot 解耦（PR-3）**：Common 经 `BOOT_INFO` / `KernelMain(void)` 启动；UEFI `BOOT_CONFIG` 仅在 `HAL/X86_64/{Startup.c,BootConfig.h}` 与 ToyBoot 之间传递
- **HAL 设备门面（PR-4）**：`Block` 后端注册 + `HalDevices.h`（USB 输入 / virtio-net）；Common 不再 `#include` ATA/PCIe/XHCI/Net 驱动头
- **HAL 去 x86 命名（PR-6）**：`HalIrqVectorSet`、`HalPagePrivatizeRootSlot`、`TASK.PageRoot` / `VirtualMemory*Root|LoadPageTable`
- **调试**：`./build.sh DEBUG=1` 打开 `DebugWrite` 串口日志

---

## 目录结构

```
ToyKernel/
├── Include/             # 公共 API 头（BootTypes.h、BootInfo.h、Hal.h、Scheduler.h…）
├── Common/
│   ├── Core/            # 内核、调度、内存、进程、系统调用（仅 .c）
│   ├── Services/        # Console、Gui、FileSystem、网络服务
│   └── Library/         # Elf、Block、Gpt、Fat、UI 等（FontData.h 内部用）
├── HAL/
│   ├── X86_64/          # Startup、Platform、CPU/中断/分页 + HalPort.h
│   │   └── Drivers/     # Ata、Serial、XHCI、Net、Video
│   ├── RiscV/           # 占位
│   └── Arm64/           # 占位
├── Fonts/               # 点阵字体数据 + 注册表（Font_* API 见 Include/Font.h）
├── User/                # Ring 3 示例程序
├── Makefile
└── build.sh
```

上层经 `Include/Hal.h` 访问硬件；`HalPort.h` 与驱动头留在 `HAL/<Arch>/`。

---

## 构建

依赖：`gcc`、`ld`（x86-64 交叉或本机 64 位工具链均可）。

```bash
cd ToyKernel

./build.sh              # ARCH=x86_64，默认关闭调试日志
./build.sh DEBUG=1      # 打开 DebugWrite 串口输出
./build.sh riscv        # 仅编占位 HAL（无完整内核）
```

产物：

- `Build/Kernel.elf` — 内核
- `Build/User/*.elf` — 用户程序（hello / count / fork / catfile）
- 自动复制到 `../ToyImage/Kernel.elf`
- x86_64 下同时复制 `HELLO.ELF`、`COUNT.ELF`、`FORK.ELF`、`CAT.ELF`

更新 UEFI 引导（可选）：

```bash
cd ../ToyBoot && ./build.sh
```

---

## 在 QEMU 中运行

```bash
cd ../ToyImage
./run-split.sh        # 唯一入口：盘0 ESP，盘1 rootfs/（Kernel/THEME）
# ./run.sh            # 已转发到 run-split.sh
```

`run-split.sh` 使用发行版 OVMF pflash、USB 键鼠、virtio-net（含 UDP/TCP hostfwd）。串口输出在启动终端（`toyos>` 提示符）。启动前会把 cwd 上的 `Kernel.elf`/`THEME.CFG` 等暂存，强制 Guest 只从第二盘加载。

首次或变量盘损坏时，可用 `./run-split.sh --clean-nvram` 从 `OVMF_VARS.fd.clean` 恢复 NVRAM。

---

## 验证示例

内核 Shell（串口或 GUI 窗口）：

```text
toyos> help
toyos> ls
toyos> write NOTE.TXT hello
toyos> cat NOTE.TXT

toyos> runuser              # 内嵌 hello
toyos> exec FORK.ELF        # 预期：C → P → done（调度顺序可能交错）
toyos> exec CAT.ELF         # 读 TOYOS.ID（需卷上有该文件）

toyos> ps
toyos> ping 10.0.2.2
toyos> udplisten 5555
```

宿主机 UDP 测试（需 `run.sh` 已配置 hostfwd）：

```bash
echo hello | nc -u 127.0.0.1 5555
```

---

## 系统调用（双路径，互不耦合）

| 路径 | 入口 | 返回 | 演示 |
|------|------|------|------|
| **legacy** | `int 0x80` → IDT → `Isr128` | `iretq` | `HELLO.ELF` / `FORK.ELF` |
| **快速** | `syscall` → `LSTAR`/`SyscallEntry` | `sysretq`（同任务） | `SYSHELLO.ELF` / `SYSFORK.ELF` |

号表与参数约定相同（`rax` = 号，`rdi`/`rsi`/`rdx` = 参数）。`SyscallDispatch` 共用；两条入口桩互不调用。

| 号 | 名称 | 说明 |
|----|------|------|
| 0 | exit | `rdi` = 退出码 |
| 1 | write | `rdi`=fd，`rsi`=buf，`rdx`=len |
| 2 | open | `rdi`=路径，返回 fd |
| 3 | read | `rdi`=fd，`rsi`=buf，`rdx`=len |
| 4 | close | `rdi`=fd |
| 5 | fork | 父返回子槽位+1，子返回 0 |
| 6 | wait | `rdi`=options；返回子 pid（`rdx`=退出码）；`WNOHANG` 时无僵尸返回 0 |
| 7 | yield | 主动让出 CPU |

详见 `Include/Syscall.h`。

---

## 已知限制

- FAT：8.3 + LFN；`mkdir`/`rmdir`；写 ≤1MB；无完整 Unicode 控制台渲染
- TCP：无重传与滑动窗口
- fork：COW 用户页；`wait` 支持 `WNOHANG`
- 最多 8 个任务槽
- `riscv` / `arm64` 尚未实现完整启动

---

## 文档与后续

- [`架构分层.md`](架构分层.md) — 分层、对外接口、禁止跨层依赖（PR-L0）
- [`结构说明.md`](结构说明.md) — 启动流程、源文件职责、阅读顺序
- [`路线图.md`](路线图.md) — 阶段规划与待办
- 计划中的后续：NVMe/virtio-blk Block 后端、真机安装器

---

## 许可证

与 ToyOS 仓库整体策略一致；若单独开源请在此补充 LICENSE。
