# ToyKernel

ToyOS 的裸机内核（x86-64 为主）。与 [ToyBoot](../ToyBoot/)（UEFI 引导）和 [ToyImage](../ToyImage/)（QEMU 镜像与启动脚本）配合，构成完整的教学/实验用操作系统。

更细的模块说明见 [`结构说明.md`](结构说明.md)，发展规划见 [`路线图.md`](路线图.md)。

---

## 当前能力概览

| 方向 | 状态 | 说明 |
|------|------|------|
| 虚拟内存 + Ring 3 | ✅ | 四级页表、用户段、`int 0x80` 系统调用 |
| 进程隔离 | ✅ | 独立地址空间、`exec`、**fork / wait / yield** |
| 文件与存储 | ✅ | ATA PIO、GPT、FAT 根目录读/写、双盘挂载 |
| 图形界面 | ≈ 可用 | 多窗口、USB 键鼠、**标题栏拖动** |
| 网络 | 已启动 | virtio-net、ping、UDP、简易 TCP echo |
| 多核 SMP | 未开始 | — |

---

## 近期更新

### 进程与用户态（阶段 2）

- **fork / wait / yield**：`VirtualMemorySpaceClone` 深拷贝用户区；子进程 exit 变 zombie，父进程 `wait` 阻塞直至收尸
- **系统调用扩展**：`open` / `read` / `close` / `fork` / `wait`；每任务 FD 表（打开时整文件读入内核缓冲）
- **用户程序**：`HELLO.ELF`、`COUNT.ELF`、`FORK.ELF`、`CAT.ELF`
- **多任务内核栈修复**：每个用户任务切换时设置独立 TSS `RSP0`（`ArchSetRsp0`），避免子进程 syscall 覆盖父进程中断帧导致 `exec FORK.ELF` 失败

### 存储与启动

- **Block + GPT + FAT**：块设备抽象、分区解析、FAT16/32 根目录读写（8.3 短名，写 ≤64K）
- **卷选择**：优先挂载含 `TOYOS.ID` 的 FAT 卷
- **双盘 QEMU**：`ToyImage/run-split.sh` — 盘 0 为 Boot/ESP，盘 1 为 `rootfs/`

### 图形与输入

- **GUI**：Shell 窗口、焦点、标题栏拖动（`VideoCopyRect` 局部刷新）
- **USB**：xHCI 键盘 + tablet 鼠标

### 网络

- **virtio-net**：ARP、ICMP ping
- **UDP / TCP**：`udplisten`、`udpsend`、`tcplisten`（TCP 无重传，仅供 QEMU 联调）

### 架构与工程

- **HAL 分层**：端口 I/O 为 `HalIoRead/Write*`；设备驱动在 `HAL/<Arch>/Drivers/`
- **目录重构（PR-1/2）**：`Common/{Core,Services,Library}`、`Include/` 公共头、`HAL/{X86_64,Arm64,RiscV}/Drivers/`
- **Boot 解耦（PR-3）**：Common 经 `BOOT_INFO` / `KernelMain(void)` 启动；UEFI `BOOT_CONFIG` 仅在 `HAL/X86_64/{Startup.c,BootConfig.h}` 与 ToyBoot 之间传递
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

./run.sh              # 单盘：当前目录整盘映射为 FAT
./run-split.sh        # 双盘：盘 0 Boot，盘 1 rootfs/
```

`run.sh` / `run-split.sh` 使用发行版 OVMF pflash、USB 键鼠、virtio-net（含 UDP/TCP hostfwd）。串口输出在启动终端（`toyos>` 提示符）。

首次或变量盘损坏时，`run-split.sh` 会从 `OVMF_VARS.fd.clean` 恢复 NVRAM。

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

## 系统调用（int 0x80）

| 号 | 名称 | 说明 |
|----|------|------|
| 0 | exit | `rdi` = 退出码 |
| 1 | write | `rdi`=fd，`rsi`=buf，`rdx`=len |
| 2 | open | `rdi`=路径，返回 fd |
| 3 | read | `rdi`=fd，`rsi`=buf，`rdx`=len |
| 4 | close | `rdi`=fd |
| 5 | fork | 父返回子槽位+1，子返回 0 |
| 6 | wait | 阻塞等待子进程，返回子 pid |
| 7 | yield | 主动让出 CPU |

详见 `Include/Syscall.h`。

---

## 已知限制

- FAT：仅根目录、8.3 短名，无子目录/LFN/删除
- TCP：无重传与滑动窗口
- fork：整页拷贝，无 COW
- 最多 8 个任务槽
- `riscv` / `arm64` 尚未实现完整启动

---

## 文档与后续

- [`结构说明.md`](结构说明.md) — 启动流程、源文件职责、阅读顺序
- [`路线图.md`](路线图.md) — 阶段规划与待办
- 计划中的后续重构（PR-4）：Block/PCIe 接口分层、文档收尾

---

## 许可证

与 ToyOS 仓库整体策略一致；若单独开源请在此补充 LICENSE。
