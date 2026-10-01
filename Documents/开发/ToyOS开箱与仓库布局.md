# ToyOS 开箱与仓库布局

> **状态**：**规格活文档**（2026-10-02）；[`PR-BOX-0`](../路线图.md#pr-box-0) **✅ TG**；★ [`PR-BOX-1`](../路线图.md#pr-box-1)（Shell 历史）。  
> **来源**：[`待做/新需求.md`](../待做/新需求.md)（需求原文；实现以**本文**为准）。  
> **迁移策略（本机）**：当前 **`…/edk2/` 整树当作备份，先不动、不就地改名**；后续从该树**逐步拷出/迁出**到家目录 **`~/ToyOS`**（见 §5.0）。脚本仍按 `$TOYOS_ROOT` 自定位，迁完后权威根即 `~/ToyOS`。  
> **路径铁律**：命令以 **ToyOS 树根**为准（可任意摆放）；**本柱迁移动作的目标根 = `~/ToyOS`**。禁止写死用户名（如 `/home/tank/...`）。  
> **目标**：外人拿到 `ToyOS` 树 → 一键装依赖 → 即可编、跑、测；仓库树清晰；启动/桌面有 Logo 与网络三态图标。  
> **不做（本柱）**：公网分发、改课设 ABI、iwl/xhci 长函数债、BOOT-fast-5（暂不做）；**删除或就地拆毁现网 `edk2/` 备份**。

---

## 路径约定（全柱）

**权威根 `TOYOS_ROOT`** = 同时含有 `ToyKernel/`、`ToyBoot/`、`ToyImage/`、`Scripts/`（迁完后还有 `EDK2/`）的那一层目录。  
脚本**不绑死**家目录；**本柱迁移的落点约定为 `~/ToyOS`**（`$HOME/ToyOS`），与「备份仍留在原 `edk2/`」并存。

| 写法 | 含义 | 是否允许 |
| ---- | ---- | -------- |
| `$TOYOS_ROOT` | 环境变量显式指定树根 | **允许**（优先） |
| 脚本自定位 | `Scripts/*.sh` → `$(cd "$(dirname "$0")/.." && pwd)` | **默认**（未设 env 时） |
| `$TOYOS_ROOT/ToyKernel` 等 | 文档中的仓内路径 | **标准写法** |
| `./Scripts/build.sh`（已 `cd $TOYOS_ROOT`） | 相对短写 | 允许 |
| **`~/ToyOS`** | **本柱迁移动作的目标根** | **迁移落点** |
| `/opt/ToyOS`、U 盘等 | 他人拷贝后的摆放 | **允许**（脚本仍自定位） |
| 当前 `…/edk2/` | **备份 / 迁出源**；BOX-5 前以它为工作源 | **保留，不就地改名删除** |
| `/home/tank/...` 写死用户 | 绑定某台机器账号 | **禁止** |

**解析顺序（所有 `Scripts/*` 必须遵守）**

1. 若已设置 `TOYOS_ROOT`：校验其下存在 `ToyKernel`+`Scripts`（迁完后再验 `ToyBoot`/`ToyImage`）；失败则报错退出。  
2. 否则：以**本脚本所在目录的父目录**为根（即 `…/ToyOS/Scripts/foo.sh` → `…/ToyOS`）。  
3. 此后一切路径用 `"$TOYOS_ROOT/…"` 拼接；**禁止**再读 `$HOME` 拼树根。

文档示例可写 `TOYOS_ROOT=/path/to/ToyOS` 或「先 `cd` 到树根」；开箱一句话里用 `<TOYOS_ROOT>` 占位。常见例：`export TOYOS_ROOT=~/ToyOS`（**例示，非强制**）。

### 环境激活（对标 EDK2 `source edksetup.sh`）

**EDK2 怎么做到敲 `build`**

| 步 | 做什么 |
| -- | ------ |
| 1 | 在树根 **`source edksetup.sh`**（必须 source，不能 `./edksetup.sh`） |
| 2 | 设 **`WORKSPACE=$PWD`**，并 `source BaseTools/BuildEnv` |
| 3 | BuildEnv 把 **`BaseTools/Bin/…` prepend 进当前 shell 的 `PATH`** |
| 4 | 该目录里有可执行文件 **`build`**，故当前终端可直接打 `build` |
| 5 | `Conf/BuildEnv.sh` 会缓存 WORKSPACE/PATH，下次 source 更快 |

子进程改不了父 shell 的环境，所以**每个新终端都要再 source 一次**（可选写入 `~/.bashrc`，非必须）。

**ToyOS 对等方案（本柱采纳）**

| 项 | 约定 |
| -- | ---- |
| 激活 | 树根 **`source Scripts/env.sh`**（可选根上 `toyosetup.sh` 转调，手感对标 `edksetup.sh`） |
| 作用 | ① 设定/校验 `TOYOS_ROOT`；② 把 `$TOYOS_ROOT/Scripts` 加入 `PATH`（或定义 shell function）；③ 读默认配置 |
| 默认配置 | 树根 **`Config.txt`**（可提交）；本机可用 **`Config.local.txt`** 覆盖（gitignore） |
| 默认行为 | 激活后敲 **`build`** ≡ `build.sh toyos <默认 arch>`；`build toyboot` / `build all arm64` 可覆盖 |

`Config.txt` 草案：

```text
DEFAULT_TARGET=toyos          # toyos | toyboot | all | sdk
DEFAULT_ARCH=x86              # x86 | arm64 | riscv
```

优先级：**命令行 > Config.local.txt > Config.txt > 内置默认（toyos + x86）**。

```bash
cd /any/where/ToyOS
source Scripts/env.sh
build                 # → ToyKernel
build toyboot
run
test smoke
```

**不做**：强制全局安装名为 `build` 的系统命令；要求永不 source 也能在任意 cwd 靠魔法找到树（未 source 时仍可用 `"$TOYOS_ROOT/Scripts/build.sh"` 全路径）。

---

## 〇、结论（能否做到）

| # | 需求摘要 | 结论 | 依据 / 落刀 |
| - | -------- | ---- | ----------- |
| 1 | Shell ↑↓ 历史命令 | **能做** | Console/Shell 行编辑；现无方向键史 → [`PR-BOX-1`](../路线图.md#pr-box-1) |
| 2 | 顶层为 `$TOYOS_ROOT/{ToyKernel,ToyBoot,ToyImage,EDK2,Scripts}`（可任意摆放） | **能做**（重） | 现为 `…/edk2/` 下三仓+整树 EDK2 → [`PR-BOX-5`](../路线图.md#pr-box-5)… |
| 3 | ToyKernel 不依赖 edk2 / ToyBoot 源码 | **能做** | 已独立 git；清残留 `#include`/脚本假设 → [`PR-BOX-7`](../路线图.md#pr-box-7) |
| 4 | ToyBoot 极简（只启核） | **能做**（边界刀） | 现已较瘦；钉「只加载 Kernel」契约 → [`PR-BOX-6`](../路线图.md#pr-box-6) |
| 5 | 启动 Logo 占位（ToyOS：锤/球/弹弓） | **能做** | GOP 早期画占位图；可后换资产 → [`PR-BOX-2`](../路线图.md#pr-box-2) |
| 6 | Logo 后进桌面须清屏 | **能做** | 现 Boot 侧清屏；Logo 改 Kernel 侧则桌面 Ready 前清 → 同 BOX-2 |
| 7 | 任务栏网络：三态 Logo 替 IP 字 | **能做** | `DesktopNetTray.c` 现 `HalNetFormatIp` → [`PR-BOX-3`](../路线图.md#pr-box-3) |
| 8 | 改名 Tty→TTY、Usb→USB、Sdk→SDK | **能做** | 标识/文档/路径分批；ABI 慎动 → [`PR-BOX-3`](../路线图.md#pr-box-3) |
| 9 | 脚本无写死用户名；`$TOYOS_ROOT` + `source env.sh` / Config | **能做** | [`PR-BOX-4`](../路线图.md#pr-box-4) |
| 10 | 一键安装宿主依赖 | **能做** | `$TOYOS_ROOT/Scripts/bootstrap.sh` → 同 BOX-4 |
| 11 | 树放任意处 + source/一键安装 = 可开发测试 | **能做** | BOX-4 + BOX-5 收官验收 |

---

## 〇′、一句话收官演示

```text
# 任意机器、任意目录
cp -a <拿到的ToyOS树> /path/to/ToyOS
cd /path/to/ToyOS
source Scripts/env.sh                          # 对标 edksetup；设 TOYOS_ROOT + PATH/函数
# （可选）编辑 Config.txt：DEFAULT_TARGET=toyos DEFAULT_ARCH=x86
build                                          # 默认编 ToyKernel
build toyboot                                  # 编 Boot
prepare-fs                                     # 或: prepare-fs x86
run                                            # QEMU
test smoke                                     # 冒烟
# 未 source 时仍可用全路径: ./Scripts/build.sh toyos x86
# 开机见 Logo 占位 → 清屏 → 桌面；任务栏网态三图标；Shell ↑↓ 历史
```

---

## 一、目标树（迁完后）

```text
$TOYOS_ROOT/                    # 树根（任意摆放；脚本自定位或 export TOYOS_ROOT）
├── ToyKernel/                  # 独立 git；内核实现（可被 Scripts/build.sh 调用）
├── ToyBoot/                    # 独立 git；极简 UEFI
├── ToyImage/                   # 独立 git；RootFs / Esp / 夹具（脚本逻辑上收 Scripts/）
├── EDK2/                       # **裁剪后**的 EDK2 202408 子集（无 .git）；仅供 ToyBoot 编 BOOTX64
└── Scripts/                    # **对外唯一入口**：build / run / test / prepare-fs / bootstrap …
```

### 1.0 `Scripts/` 设计（同类只留一个 · 参数分流）— **可行，本柱采纳**

**原则**：同一类动作只保留 **一个** 顶层脚本；用**位置参数 / 子命令**决定编谁、跑哪架构、测哪套。仓内可保留薄包装（旧 `build.sh` 一行 `exec` 转发），但**文档与开箱只教 `$TOYOS_ROOT/Scripts/`**（或树内 `./Scripts/`）。

#### 1.0.1 对外入口（只这些文件名）

| 顶层脚本 | 职责 | 参数约定（草案 · BOX-4/5 钉死） |
| -------- | ---- | -------------------------------- |
| `bootstrap.sh` | 一键装宿主依赖 | 无目标参数；幂等 |
| `env.sh` | **须 source**：设 `TOYOS_ROOT`、PATH/函数、读 `Config.txt` | `source Scripts/env.sh`（对标 edksetup） |
| `build.sh` | 编译 | `build.sh [<toyos\|toyboot\|sdk\|all>] [<x86\|arm64\|riscv>] [DEBUG=1 …]`；缺省读 Config |
| `run.sh` | 启动 QEMU | `run.sh [<x86\|arm64\|riscv>] [split\|virt] …` |
| `test.sh` | 冒烟 / 自动测试 / 宿主单测 | `test.sh <套件> [<arch\|host>] …` |
| `prepare-fs.sh` | 准备 RootFs / Esp / virt 盘镜像 | `prepare-fs.sh [<arch>] [opts]` |
| `sync.sh` | 同步到 U 盘 / NUC | `sync.sh <usb\|nuc\|kernel> [opts]` |
| `store.sh` | LAN 商店导出 / 本地 HTTP 服务 | `store.sh <export\|serve> [out-dir]` |
| `pack.sh` | 课包打进 `Store/packages/<id>/` | `pack.sh <id> <elf-path> [file-name]` |
| `usb.sh` | 做启动 U 盘 | `usb.sh make [opts]` |
| `measure.sh` | 开机墙钟测量等工具 | `measure.sh boot [opts]` |
| `desktop.sh` | 宿主桌面辅助（Dock 图标等） | `desktop.sh dock-icon` |

根目录另可放：`Config.txt`（默认）、`Config.local.txt`（本机覆盖，gitignore）、可选 `toyosetup.sh`（`source` 转调 `Scripts/env.sh`）。

激活后 PATH 上的短名：`build`/`run`/`test`/`prepare-fs`/… → 对应 `Scripts/*.sh`（或同名 function 调脚本）。

#### 1.0.2 现网 `.sh` 全表 → 收拢目标（2026-10-01 盘点）

> **范围**：ToyKernel / ToyBoot / ToyImage 自有脚本。  
> **不收拢**：`ThirdParty/lwip/**`、`Tools/Extract/**`（工具链自带）、裁剪后 `EDK2/` 上游脚本（仅 ToyBoot 内部 `source edksetup`）。

| 现路径 | 一句话 | 收拢到 |
| ------ | ------ | ------ |
| `ToyKernel/build.sh` | 编 Kernel（ARCH=…） | `build.sh toyos <arch>` |
| `ToyBoot/build.sh` | 编 BOOTX64（EDK2） | `build.sh toyboot <arch>`（首期 mainly x86） |
| `ToyKernel/Tools/build-sdk.sh` | 打包 ToySDK | `build.sh sdk <arch>` |
| `ToyImage/Scripts/run-split.sh` | x86 双盘 QEMU（主入口） | `run.sh x86 split`（默认） |
| `ToyImage/Scripts/run.sh` | 已废弃→转 run-split | **删除对外名**；兼容期转发 `run.sh x86` |
| `ToyImage/Scripts/run-virt-arm.sh` | aarch64 virt | `run.sh arm64 virt` |
| `ToyImage/Scripts/run-virt-riscv.sh` | riscv64 virt | `run.sh riscv virt` |
| `ToyImage/Scripts/run-virt-common.sh` | virt 公共逻辑 | `Scripts/lib/run-virt-common.sh` |
| `ToyImage/Scripts/toy-qemu-lib.sh` | QEMU/NVRAM/THEME 公共 | `Scripts/lib/toy-qemu-lib.sh` |
| `ToyImage/Scripts/prepare-rootfs.sh` | 准备 RootFs/X64 | `prepare-fs.sh x86` |
| `ToyImage/Scripts/prepare-virt-rootfs.sh` | Arm/RiscV virt 盘镜像 | `prepare-fs.sh arm64\|riscv` |
| `ToyImage/Scripts/smoke-boot.sh` | x86 无头冒烟 ready | `test.sh smoke x86` |
| `ToyImage/Scripts/smoke-virt.sh` | Arm+RiscV virt 冒烟 | `test.sh smoke arm64` / `test.sh smoke riscv`（或 `test.sh smoke-virt`） |
| `ToyImage/Scripts/smoke-install.sh` | install 第三盘冒烟 | `test.sh smoke-install x86` |
| `ToyImage/Scripts/smoke-msc.sh` | USB MSC 冒烟 | `test.sh smoke-msc x86` |
| `ToyImage/Scripts/test-fs.sh` | FS 自动测 | `test.sh fs x86` |
| `ToyImage/Scripts/test-user.sh` | 用户态自动测 | `test.sh user x86` |
| `ToyImage/Scripts/test-shell.sh` | Shell 自动测 | `test.sh shell x86` |
| `ToyImage/Scripts/test-enosys.sh` | ENOSYS 测 | `test.sh enosys x86` |
| `ToyImage/Scripts/test-mod-verify.sh` | bundle expect 串跑 | `test.sh mod-verify x86` |
| `ToyImage/Scripts/test-bundle-*.exp` | expect 夹具（非 .sh） | 随 `test.sh mod-verify`；资产进 `Scripts/lib/expect/` 或仍留 Image |
| `ToyKernel/Tools/Scripts/test-all.sh` | 开课前统一自动入口 | `test.sh all host`（或 `test.sh all x86` 按现语义） |
| `ToyKernel/Tools/Scripts/runtests.sh` | 宿主 scheduler/memory/fs 单测 | `test.sh unit host`（参数透传 suite） |
| `ToyImage/Scripts/sync-usb.sh` | 同步到 U 盘 ESP+TOYOS | `sync.sh usb` |
| `ToyImage/Scripts/sync-nuc.sh` | 同步到 NUC SSD | `sync.sh nuc` |
| `ToyImage/Scripts/sync-kernel-usb.sh` | → sync-usb --kernel-only | `sync.sh kernel`（或 `sync.sh usb --kernel-only`） |
| `ToyImage/Scripts/make-usb-stick.sh` | 格式化/做成启动盘 | `usb.sh make` |
| `ToyImage/Scripts/measure-boot.sh` | 开机墙钟 | `measure.sh boot` |
| `ToyImage/Scripts/dock-icon.sh` | GNOME Dock 图标 | `desktop.sh dock-icon` |
| `ToyKernel/Tools/Scripts/export-store-lan.sh` | 导出 LAN 仓库树 | `store.sh export` |
| `ToyKernel/Tools/Scripts/serve-store-lan.sh` | 导出 + HTTP :8080 | `store.sh serve` |
| `ToyKernel/Tools/Scripts/pack-app.sh` | ELF → Store/packages | `pack.sh` |

**合计（自有 .sh）**：Kernel 7 + Boot 1 + Image 23 = **31**；另 3 个 `.exp` 挂 `test.sh`。全部纳入上表，无遗漏（第三方/工具链除外）。

#### 1.0.3 为何可行 / 不做

| 点 | 说明 |
| -- | ---- |
| 已有分流基础 | 现网已是「多文件 = 多参数」；收成单入口是改名+case，不是新能力 |
| Boot≠Kernel 工具链 | 一个 `build.sh` 内 `case` 即可 |
| 兼容过渡 | 旧路径保留一行 `exec "$TOYOS_ROOT/Scripts/…"`（或自定位）至 BOX-5 收官后再删对外名 |

**不做**：再增 `build-toyos.sh` / `run-virt-arm.sh` 等平行对外名；把 EDK2 `edksetup.sh` 当教学入口；收拢 ThirdParty/Extract 里的上游脚本。

### 1.1 与现状对照

| 现状 | 目标 |
| ---- | ---- |
| 顶层常为 `…/edk2/`（整树上百万行 EDK2 + 三仓嵌套） | **`$TOYOS_ROOT/`**（名建议 `ToyOS`，位置不限） |
| `ToyKernel`/`ToyBoot`/`ToyImage` 已是独立 git，但旁挂完整 EDK2 | **`$TOYOS_ROOT/EDK2/`** 仅保留 ToyBoot 构建所需 |
| 上表 31 个自有 `.sh` 林立 | **`$TOYOS_ROOT/Scripts/`** 十余个单入口 + `lib/`；见 §1.0.2 |
| 文档写死 `tank` / 绑死家目录 | 以 `$TOYOS_ROOT` / 脚本自定位为准；示例可用任意路径 |

### 1.2 硬约束

| # | 约束 |
| - | ---- |
| 0 | **路径根 = `$TOYOS_ROOT`（自定位）**；可放任意目录；禁止写死用户名（见文首「路径约定」） |
| 0b | **Scripts 同类唯一入口**：§1.0.1 表内文件名；现网 31 个自有 `.sh` 按 §1.0.2 收拢；禁止再增平行对外名 |
| 1 | **三仓 git 历史保留**（ToyKernel / ToyBoot / ToyImage）；搬家用目录迁 + 改相对路径，不强制 monorepo |
| 2 | EDK2 子集 **去掉 `.git`**；钉版本说明 **202408**；能编出当前 `BOOTX64.EFI` |
| 3 | ToyKernel **`./build.sh` 不读** ToyBoot / EDK2 头文件或库 |
| 4 | 新 `.c` ≤300；三架构可编；涉及桌面/引导则 `smoke-boot` |
| 5 | 改名刀：**用户可见字符串 / 文档 / 路径** 优先；syscall/公开 ABI 另表评估，禁止静默破坏课设 |
| 6 | Logo 首刀 = **占位像素/BMP**；精美资产可后换，不挡收官 |
| 7 | 一次一刀；大搬家（BOX-5…）不与体验小刀（BOX-1…3）同提交搅在一起 |

---

## 二、现网锚点（实现时对照）

| 主题 | 现状锚点 | 备注 |
| ---- | -------- | ---- |
| Shell 行编辑 | Console / Shell 任务；HID `HID_KEY_UP` 已有 | 缺「命令历史环 + ↑↓ 填回」 |
| 任务栏网态 | `Common/Services/Desktop/DesktopNetTray.c` | 现格式化 IP；改为三态图标 |
| 开机清屏 | `ToyBoot/Video/BootVideo.c` 设分辨率后清屏 | Logo 若画在 Kernel，桌面 `LoadWallpaper` 前再清一次 |
| 同步文档 | 仓外 `edk2/SYNC.md`；常写死 `/home/tank/...` | BOX-4 起一律 `$TOYOS_ROOT/...` |
| 宿主依赖 | 无统一 bootstrap；靠 README 手装 | BOX-4：`$TOYOS_ROOT/Scripts/bootstrap.sh` |
| 编/跑/测入口 | Kernel/Boot/Image 多套 `build`/`run-*`/`smoke-*` | BOX-4/5：§1.0 参数化单入口 |

---

## 三、体验轨（不依赖大搬家，可先做）

### 3.1 Shell 历史 · BOX-1

| 项 | 内容 |
| -- | ---- |
| 做 | 环形历史（建议 ≥32 条）；↑ 更旧 / ↓ 更新；到顶/底钳制；新提交回最新空行 |
| 不做 | 持久化到盘；Ctrl-R 搜索；多行编辑 |
| 验收 | QEMU Shell：连敲几条命令后 ↑↓ 能回到先前行并回车重跑 |

### 3.2 启动 Logo + 清屏 · BOX-2

| 项 | 内容 |
| -- | ---- |
| 做 | 启动早期（Kernel GOP 就绪后、桌面壁纸前）画 **ToyOS 占位 Logo**：T=锤、O=足球、Y=弹弓，旁衬 OS 字样（可用内嵌像素/`Assets` BMP） |
| 清屏 | Logo 展示结束后、进桌面绘制前 **整屏清黑/底色**（不再依赖「仅 Boot 清过一次」的隐含前提） |
| 不做 | 动画引擎；强制品牌规范审稿；挡住 `ToyOS ready` 串口语义 |
| 验收 | QEMU/NUC：可见占位 Logo → 清屏 → 桌面；`smoke-boot` 仍 PASS |

### 3.3 网络三态图标 + 命名 · BOX-3

| 项 | 内容 |
| -- | ---- |
| 网态 | 任务栏时钟左侧：**有线 / Wi‑Fi / 无网络** 三图标（可先 16×16 占位图）；**默认不显示 IP 字符串**（调试可 `dbset`/编译旗打开） |
| 改名 | 用户可见与文档优先：`Tty`→`TTY`、`Usb`→`USB`、`Sdk`→`SDK`；代码符号分批（见命名规范），公开 ABI 变更单列 |
| 不做 | 新画复杂拟物图标集；改 DHCP/路由逻辑 |
| 验收 | 有线 up / Wi‑Fi up / 全断 三态可辨；文档与 UI 字符串无旧大小写混用（本刀范围清单勾完） |

---

## 四、开箱脚本轨 · BOX-4

| 项 | 内容 |
| -- | ---- |
| 路径 | 全脚本 **`TOYOS_ROOT` 解析**；**`source Scripts/env.sh`** + 根 **`Config.txt`**（默认 toyos/x86）；禁止写死用户名 |
| 入口 | §1.0 骨架（含 `env.sh`）；激活后短名 `build`/`run`/`test`… |
| bootstrap | `$TOYOS_ROOT/Scripts/bootstrap.sh` |
| 验收 | source 后直接 `build` 编 Kernel；`build toyboot` 可覆盖；**非家目录**仍可；未 source 时 `./Scripts/build.sh` 仍可用 |
| 不做 | 强制写进全局 bashrc；系统级安装 `build`；平行 `build-toyos.sh`；再要求必须 `$HOME/ToyOS` |

---

## 五、仓库布局轨（重）

### 5.0 本机迁移策略（先备份、再逐步迁出）

```text
[备份 · 先不动]
  …/edk2/          ← 现网整树（含 ToyKernel/ToyBoot/ToyImage + 完整 EDK2）
                     BOX-1…4 体验刀可仍在此树开发；禁止就地改名为 ToyOS / 删树

[目标 · 逐步生成]
  ~/ToyOS/         ← 新权威根（迁完后 TOYOS_ROOT 默认指这里）
    ToyKernel/       从 edk2/ToyKernel 拷出/同步（保留独立 .git）
    ToyBoot/         从 edk2/ToyBoot 拷出/同步
    ToyImage/        从 edk2/ToyImage 拷出/同步
    EDK2/            BOX-6：从 edk2 裁剪后放入（无 .git）
    Scripts/         BOX-4/5：新建单入口；再把旧脚本逻辑收敛进来
```

| 规则 | 说明 |
| ---- | ---- |
| 源 | 始终从 **`edk2/`** 读出；迁出用 **拷贝 / rsync / git clone 本地路径**，不 `mv` 掉备份 |
| 汇 | 写入 **`~/ToyOS/`**；日常验证以 `export TOYOS_ROOT=$HOME/ToyOS`（或自定位）为准 |
| 节奏 | **BOX-1…3** 可继续在 `edk2/ToyKernel` 改代码；**BOX-4** 可先在 `~/ToyOS/Scripts` 搭骨架并转发回 edk2；**BOX-5** 起批量同步三仓进 `~/ToyOS`；**BOX-6/7** 在新树裁 EDK2 / 验独立 |
| 双轨期 | 两套树可短暂并存；文档写清「开发以哪棵为准」；收官后推荐只维护 `~/ToyOS`，`edk2/` 仅作冷备份 |
| 禁止 | 未经验收就删除 `edk2/`；把 `edk2` 目录就地 rename 成 `ToyOS` 冒充迁移完成 |

### 5.1 迁目录 · BOX-5

| 项 | 内容 |
| -- | ---- |
| 做 | 按 §5.0 把三仓同步进 **`~/ToyOS/`**；新建/补齐 `Scripts/`；旧 `ToyImage/Scripts/*` 收敛进参数化入口（实现可放 `Scripts/lib/`）；**保留 `edk2/` 备份** |
| 验收 | `export TOYOS_ROOT=$HOME/ToyOS`；`"$TOYOS_ROOT/Scripts/build.sh" all x86` 与 `test.sh smoke x86` 闭环；三仓 git 仍独立；`edk2/` 仍在且可对照 |
| 风险 | SYNC/CI/肌肉记忆；迁完双机按 `~/ToyOS/Scripts` 做 `TB` |

### 5.2 EDK2 裁剪 + Boot 边界 · BOX-6

| 项 | 内容 |
| -- | ---- |
| EDK2 | 只留 **202408** 中 ToyBoot 构建 `BOOTX64.EFI` 所需；**删除 `.git`**；写 `$TOYOS_ROOT/EDK2/README-TRIM.md`（保留包列表 + 如何再裁） |
| ToyBoot | 契约：**读 Kernel → 设显示（如需）→ ExitBootServices → 跳入口**；不承担桌面/Logo 终态（Logo 在 Kernel） |
| 验收 | `$TOYOS_ROOT/EDK2` 裁剪树能编出与现网兼容的 Boot；QEMU split 启动 PASS |

### 5.3 Kernel 独立 · BOX-7

| 项 | 内容 |
| -- | ---- |
| 做 | 审计并去掉对 EDK2/ToyBoot 源码路径的编译/头依赖；文档写明「在 `$TOYOS_ROOT/ToyKernel` 只编 Kernel」步骤 |
| 验收 | 仅有 `$TOYOS_ROOT/ToyKernel` + 工具链即可 `./build.sh`（不要求旁挂 EDK2）；产物可被 `$TOYOS_ROOT/ToyBoot` 加载 |

---

## 六、PR 切分（执行序）

| 刀 | 一句话 | 依赖 |
| -- | ------ | ---- |
| **PR-BOX-0** | 本文规格冻结 + 路线图升 ★ | — |
| **PR-BOX-1** | Shell ↑↓ 历史 | BOX-0 |
| **PR-BOX-2** | 启动 Logo 占位 + 进桌面清屏 | BOX-0 |
| **PR-BOX-3** | 网络三态图标 + TTY/USB/SDK 改名 | BOX-0 |
| **PR-BOX-4** | `$TOYOS_ROOT` 自定位 + 单入口 Scripts 骨架 | BOX-0（可与 1–3 并行） |
| **PR-BOX-5** | 从 `edk2/` 逐步同步到 `~/ToyOS` + 脚本收敛 | BOX-4 |
| **PR-BOX-6** | EDK2 202408 裁剪去 `.git` + Boot 极简契约 | BOX-5 |
| **PR-BOX-7** | ToyKernel 零依赖 edk2/ToyBoot 源码验收 | BOX-5（可与 6 部分并行） |

**建议课堂演示序**：0 → 1 → 2 → 3（体验可见）→ 4（别人能开箱）→ 5 → 6 → 7（仓库干净）。

### 6.1 验收总表

| 刀 | 通过标准 |
| -- | -------- |
| BOX-0 | 本文与路线图刀卡无歧义；★ = BOX-0 |
| BOX-1 | ↑↓ 历史可用 |
| BOX-2 | Logo 可见 + 清屏后桌面正常；smoke PASS |
| BOX-3 | 三态图标；改名清单勾完 |
| BOX-4 | `build.sh toyos\|toyboot <arch>` / `run.sh` / `test.sh` 可用；bootstrap 幂等；**非家目录摆放仍可跑**；无写死用户名 |
| BOX-5 | `~/ToyOS` 可编可烟测；`edk2/` 备份仍在；旧平行脚本名退出开箱文档 |
| BOX-6 | `$TOYOS_ROOT/EDK2` 裁剪后能出 Boot |
| BOX-7 | `$TOYOS_ROOT/ToyKernel` 单仓 `./build.sh` 成功 |

---

## 七、本柱不做

- 把三仓强制合成单一 git monorepo  
- 保留完整上游 EDK2 远程跟踪 / 再 submodule 拉取百万行  
- Logo 商业设计定稿、多主题启动动画  
- 任务栏恢复默认常显 IP（可留调试开关）  
- 改名时无评估地批量改 syscall 号/课设 ABI  
- **就地拆毁 / rename 现网 `edk2/` 备份**（须先完成 `~/ToyOS` 验收）
---

## 八、原文对照（`待做/新需求.md`）

| 原文条 | 本文落点 |
| ------ | -------- |
| 1 方向键历史 | §3.1 / BOX-1 |
| 2a–g 目录大改 | §一 / §五 / BOX-5…6（根 = `$TOYOS_ROOT`） |
| 3 Kernel 独立 | §5.3 / BOX-7 |
| 4 Boot 极简 | §5.2 / BOX-6 |
| 5–6 Logo + 清屏 | §3.2 / BOX-2 |
| 7 网络 Logo | §3.3 / BOX-3 |
| 8 改名 | §3.3 / BOX-3 |
| 9–11 路径自定位 + 一键安装 + 开箱 | 文首路径约定 + §四 / BOX-4（+ BOX-5 收官） |
