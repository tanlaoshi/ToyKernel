# ToyKernel

ToyOS 的裸机内核（x86-64 为主）。与 [ToyBoot](../ToyBoot/)（UEFI 引导）和 [ToyImage](../ToyImage/)（QEMU 镜像与启动脚本）一起，构成完整的教学/实验用操作系统。

```
加电 → OVMF（UEFI）→ ToyBoot（BOOTX64.EFI）→ ToyKernel → 桌面 / Shell / 用户 ELF
```

**文档入口（与仓库根 [`../README.md`](../README.md) 一致）：**

| 文档 | 内容 |
|------|------|
| **本 README** / 根 README | 项目是什么、能干什么、怎么编怎么跑 |
| [`Documents/路线图.md`](Documents/路线图.md) | 当前指针、规划、同步、归档（**文首有目录**） |
| [`Documents/技术手册.md`](Documents/技术手册.md) | 架构与操作白皮书（**文首有目录**） |
| [`Documents/驱动开发指南.md`](Documents/驱动开发指南.md) | 新人写驱动：5 步 + Demo + 常见错误 |
| [`Documents/驱动匹配规范.md`](Documents/驱动匹配规范.md) | Probe 三模式；`Match` 占位（不强制改真驱动） |
| [`Documents/应用开发指南.md`](Documents/应用开发指南.md) / [`API速查.md`](Documents/API速查.md) | 新人写用户态应用 |
| [`HAL/X64/Drivers/_template/`](HAL/X64/Drivers/_template/) | 驱动拷贝源（**不**编进 Kernel） |
| [`Documents/Done/`](Documents/Done/) | 已完成专题（含应用生态规划、驱动标准化分析/拆分/设计） |

`Documents/` 正文：路线图 + 技术手册 + 驱动标准化相关页 + 应用开发指南；归档见 `Done/`。课堂讲义见 [`教学内容/`](../教学内容/)。

协作暗号（详见路线图）：**JX** = 下一刀；**TG** = 通过，只 commit；**TS** = push；**TB** = 拉同步。真机 USB 轨请先读 handoff。

---

## 当前能力概览

| 方向 | 状态 | 说明 |
|------|------|------|
| 虚拟内存 + Ring 3 | ✅ | 四级页表、用户段、`int 0x80` 与 `syscall`/`sysret` |
| 进程 | ✅ | 独立地址空间、`exec`/`fork`/`wait`/`yield`/`kill`、简易 `.so` |
| 文件与存储 | ✅ | ATA/AHCI/NVMe、GPT、FAT；多卷；`RES:`；Files 浏览器 |
| GUI | ✅ 教学级 | GOP 多窗口、主题、Settings、合成/脏 Present（G9） |
| 跨架构 virt | ✅ | Arm64/RiscV 自有 Boot + ramfb/virtio；同一套 Common Gui |
| 网络 | ✅ | virtio-net；**默认 lwIP** 用户 socket / DNS；builtin = 教学对照 |
| SMP | ✅ 演示级 | AP idle / 可偷任务；shell/gui 钉 BSP |
| 应用商店 | ✅ | `store install/remove/combo`；资源包 + 依赖 |
| **真机 UEFI PC** | ✅ **里程碑** | NUC：U 盘启动；xHCI poll；**有线键鼠分口可用**；**短按电源可关机**（2026-09-10） |

细表与缺口见 [`Documents/路线图.md`](Documents/路线图.md)（文末有 NUC 里程碑归档）。

---

## 最短上手

```bash
# 1. 编内核（会同步到 ToyImage）
cd ToyKernel && ./build.sh              # 默认 LWIP=1
# ./build.sh LWIP=0                     # 关掉 lwIP，仅 builtin 教学栈
# ./build.sh DEBUG=1

# 2. 编 UEFI 引导（可选，改 Boot 时才必须）
cd ../ToyBoot && ./build.sh

# 3. QEMU（x86 主路径）
cd ../ToyImage
./run-split.sh                 # 盘0=ESP，盘1=RootFs/X64/
./smoke-boot.sh                # 无头冒烟 → ToyOS ready
```

串口或 Shell 窗出现 `toyos>` 后：`help`、`ls`、`exec HELLO.ELF`。

**网络课默认路径**（细节见 [`ThirdParty/README.md`](ThirdParty/README.md)、[`ToyImage/QUICK_START.md`](../ToyImage/QUICK_START.md)）：

```text
# 宿主机：nc -l -p 8888
ping 10.0.2.2
lwip on
dns 10.0.2.2
exec NETLIB.ELF          # 或 NETDEMO.ELF（裸 syscall 对照）
```

### Arm64 / RiscV（virt，非 OVMF）

```bash
cd ToyKernel && ./build.sh arm64 LWIP=0   # 或 riscv；virt 尚无 LwIp 端口须 LWIP=0
cd ../ToyImage
./run-virt-arm.sh                         # / ./run-virt-riscv.sh
./smoke-virt.sh                           # 双 arch 无头冒烟
```

### 构建产物

- `Build/HAL/{X86_64,Arm64,RiscV}/Kernel.elf`
- 用户 ELF 复制到 `../ToyImage/` 与 `RootFs/X64/`
- 板包：`./build.sh arm64 BOARD=virt`；`make boards ARCH=arm64`

---

## 目录结构（摘要）

```
ToyKernel/
├── Include/          # 公共 API（BOOT_INFO、Hal*、Syscall…）
├── Common/{Core,Services,Library,Fonts}
├── HAL/{X86_64,Arm64,RiscV,Board}/
├── Assets/  User/
├── Documents/        # 路线图 + 技术手册（仅此两份正文）
├── build.sh  Makefile
└── README.md         # 本文件
```

完整文件职责与启动顺序见 [`技术手册`](Documents/技术手册.md)「目录与启动」。

---

## 系统调用（摘要）

| 路径 | 入口 | 演示 |
|------|------|------|
| legacy | `int 0x80` | `HELLO.ELF` / `FORK.ELF` |
| 快速 | `syscall` | `SYSHELLO.ELF` / `SYSFORK.ELF` |

常用号：`exit` `write` `open` `read` `close` `fork` `wait` `yield`（及 pipe/dup/brk/kill/socket…）。详见 `Include/Syscall.h` 与技术手册「用户态」。

---

## 已知限制（短表）

- FAT 写有上限；无完整 Unicode 控制台
- Builtin TCP 为教学对照；默认课走 `lwip on` + `NETLIB`/`NETDEMO`（关栈用 `LWIP=0`）
- 任务槽有限；shell/gui 钉 BSP
- 无完整 POSIX / TTF / 热加载驱动商店
- 明确不做：手机 SoC、自研编译器

更全列表见路线图「缺口 / 明确不做」。

---

## 许可证

与 ToyOS 仓库整体策略一致；若单独开源请在此补充 LICENSE。
