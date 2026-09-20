# ToyOS GitHub Actions CI/CD

> **本文是本柱总纲 + 现状分析 + PR 切分。** 排期认 [`路线图.md`](路线图.md#pr-ci)。  
> **状态（2026-09-20）**：**PR-CI-1 进行中**（增强 `build.yml`：ubuntu-24.04 + lwIP clone + artifact）。  
> **文首 ★** = 本柱 [`PR-CI-1`](路线图.md#pr-ci)。  
> **硬约束**：不改内核、不改 `build.sh` / `smoke-boot.sh`；只动各仓 `.github/workflows/*.yml`；不部署服务器；单 job `timeout-minutes` ≤ 30。

---

## 一、分析报告（任务书 5 问 + 仓库事实）

### 1.1 根目录结构：不是单仓

本机工作区是 **edk2 大树**，ToyOS 三仓只是其中子目录，但 **GitHub 上是三个独立仓库**：

| 仓 | 远程 | 默认分支 | 仓库根是什么 |
| -- | ---- | -------- | ------------ |
| **ToyKernel** | `github.com:tanlaoshi/ToyKernel.git` | **`main`** | 内核本身（`build.sh` 就在根） |
| **ToyImage** | `github.com:tanlaoshi/ToyImage.git` | **`master`** | QEMU 镜像 / `Scripts/` |
| **ToyBoot** | `github.com:tanlaoshi/ToyBoot.git` | **`main`** | UEFI `BOOTX64.EFI` |

**任务书 YAML 里的 `cd ToyKernel` / `cd ToyImage` 只适用于本机 edk2 并列目录，不能原样贴进 GitHub。**  
在 ToyKernel 仓里工作目录已经是内核根：应 `./build.sh`，产物 `Build/HAL/X64/Kernel.elf`。

edk2 根也有 `.github/`，那是 **tianocore/EDK2** 的 CI，**禁止**往那里加 ToyOS workflow。

公开/私有：本机 `gh` 未返回 visibility；按远程是 GitHub 个人仓。**阶段 1 不依赖额度数字**；私有仓注意 2000 分钟/月。

### 1.2 `ToyKernel/build.sh` 用法

路径准确：**仓根** `./build.sh`（不是 `ToyKernel/build.sh`，那是本机相对 edk2 的说法）。

```bash
./build.sh                 # 默认 ARCH=x86_64，LWIP=1
./build.sh x86_64          # 同上（多余参数当 ARCH）
./build.sh arm64           # 无 LwIp 端口时自动 LWIP=0
./build.sh riscv
./build.sh LWIP=0
./build.sh arm64 BOARD=virt
```

要点：

- 每次会 `make clean`。
- 有并列 `../ToyImage/RootFs/X64` 才拷 Kernel/演示 ELF；**纯 Kernel checkout 会 skip copy**（已为 CI 写过）。
- **`ThirdParty/lwip/` 在 `.gitignore`**，未 clone 时默认 `LWIP=1` **编不过**。CI 必须二选一：workflow 里 `git clone` lwIP，或 `./build.sh LWIP=0`。不改 `build.sh`。

产物路径：

| ARCH | ELF |
| ---- | --- |
| x86_64 | `Build/HAL/X64/Kernel.elf` |
| arm64 | `Build/HAL/Arm64/Kernel.elf` |
| riscv | `Build/HAL/RiscV/Kernel.elf` |

### 1.3 冒烟脚本路径

**不是** `ToyImage/smoke-boot.sh`。  
正确：**`ToyImage/Scripts/smoke-boot.sh`**（仓根为 ToyImage 时即 `./Scripts/smoke-boot.sh`）。

依赖：已编好的 `RootFs/X64/Kernel.elf`、`Scripts/run-split.sh`、QEMU `qemu-system-x86`、OVMF、ESP 上的 `BOOTX64.EFI`（通常已在 Image 的 `Esp/X64`）。等串口 `ToyOS ready` / `ToyOS 就绪`；默认超时 90s，可用 `SMOKE_TIMEOUT`。

### 1.4 已有 `.github`（不要当空白）

| 路径 | 现状 |
| ---- | ---- |
| `ToyKernel/.github/workflows/build.yml` | **已有（PR-Q1）**：`main` push/PR；`ubuntu-latest`；`./build.sh x86_64`；检查 ELF；**无**交叉编译、artifact、timeout |
| `ToyImage/.github/workflows/smoke.yml` | **已有**：checkout Image + `tanlaoshi/ToyKernel`；编内核；`TOY_SMP=1 SMOKE_TIMEOUT=120 ./Scripts/smoke-boot.sh`；**仅** Image 脚本路径变更才跑，**不是每次 Kernel PR** |
| `ToyBoot/` | 无 `.github` |
| `.gitee/` | **三仓皆无**（不要 Gitee Actions） |

路线图已归档：[`【归档】PR-Q1`](路线图.md) 记过这两份 workflow。本柱是 **增强**，不是从零发明。

### 1.5 任务书样例必须改掉的点

| 样例 | 问题 | 落地时 |
| ---- | ---- | ------ |
| `cd ToyKernel && ./build.sh` | Kernel 仓根没有子目录 ToyKernel | `./build.sh` |
| `cd ToyImage && ./smoke-boot.sh` | 脚本在 `Scripts/` | Image 仓：`./Scripts/smoke-boot.sh` |
| 单仓三架构 + smoke | smoke 依赖 Image/Boot 资产 | smoke **留在 ToyImage workflow**；Kernel 仓只编 |
| lint >800 行 | 现门槛是 **300**；且 `Core/` 已不在 `Common/` | 阶段 2 可选 warn-only；find 含 `ToyKernel/Core` 在 **Kernel 仓则 `Core/`** |
| `ubuntu-latest`（现有） | 任务书指定 24.04 | 阶段 1 起改 `ubuntu-24.04` |
| 无 `timeout-minutes` | 额度保护 | 每 job **20**（上限 30） |
| 默认 `LWIP=1` | CI 无 lwIP 源 | clone 或 `LWIP=0`（见下） |

**lwIP（已确认）**：workflow 内 clone，**不用** `LWIP=0`。与 [`ThirdParty/README.md`](../ThirdParty/README.md) 一致：

```bash
git clone --depth 1 --branch STABLE-2_2_0_RELEASE \
  https://github.com/lwip-tcpip/lwip.git ThirdParty/lwip
```

然后 `./build.sh`（默认 `LWIP=1`）。不改 `build.sh`、不把 lwIP 源码提交进仓库。

---

## 二、目标与硬约束

| # | 约束 |
| - | ---- |
| 1 | 不改内核 / 不改 `build.sh` / `smoke-boot.sh` |
| 2 | 只新增或改 `.github/workflows/*.yml`（三仓各自的） |
| 3 | 每阶段独立可验证；**不要一次做完三阶段** |
| 4 | GitHub Actions；不用 Gitee；不部署服务器 |
| 5 | job `timeout-minutes` ≤ 30 |
| 6 | 不往 **edk2** 上游 `.github` 塞 ToyOS CI |

---

## 三、PR 切分（3 阶段 = 3 刀）

| 序 | PR | 仓 | 交付 | 验收 |
| -- | -- | -- | ---- | ---- |
| **1** | **PR-CI-1** | **ToyKernel** | 增强现有 `build.yml`：x86_64 编通 + 上传 artifact | 本刀；Actions 绿后 ✅ |
| **2** | **PR-CI-2** | Kernel + Image | Kernel：arm64/riscv job；Image：smoke 在 Kernel 相关 PR/push 可跑（或 `workflow_call`） | 三架构绿；QEMU smoke 绿 |
| **3** | **PR-CI-3** | **ToyKernel** | `release.yml`：tag `v*` → 三架构包 + GitHub Release | `git tag v*` 后 Releases 可见 |

ToyBoot 编 EFI **不进这三刀**（可选后置）。

### 第 1 刀（阶段 1）细则 — 最小 CI

改 **`ToyKernel/.github/workflows/build.yml`**（已存在，勿另起一套抢触发）：

- `runs-on: ubuntu-24.04`
- `timeout-minutes: 20`
- `on.push/pull_request.branches: [main]`（Kernel 没有 master 作默认）
- checkout → 安装 `build-essential gcc make` → **clone lwIP** → `./build.sh` 或 `./build.sh x86_64`（默认 `LWIP=1`）
- `test -f Build/HAL/X64/Kernel.elf`
- `actions/upload-artifact@v4`，`retention-days: 7`，路径相对仓根

**不做**：arm64/riscv、smoke、lint、release。

### 第 2 刀（阶段 2）细则 — 多架构 + smoke

**Kernel `build.yml`（或拆 `ci.yml` 并让旧 build 停用，避免双跑）**：

- 三 job：`build-x86_64` / `build-arm64` / `build-riscv`
- 交叉包：`gcc-aarch64-linux-gnu` / `gcc-riscv64-linux-gnu`
- 各 job 上传对应 `Build/HAL/{X64,Arm64,RiscV}/Kernel.elf`
- arm64/riscv 用 `./build.sh arm64` / `./build.sh riscv`（脚本会自动 LWIP=0）

**Image `smoke.yml`**：

- 扩触发：不要只盯 Image 脚本路径；至少 `workflow_dispatch` + **定时或 `repository_dispatch`**，或 Kernel workflow `workflow_call` / 用 `actions/checkout` 拉 Image。
- 推荐：Kernel CI 在 x86 编完后 **checkout `tanlaoshi/ToyImage`** 到 `../ToyImage`（或 `ToyImage/` 并列），拷 ELF 进 `RootFs/X64`，再 `./Scripts/smoke-boot.sh`。这样 **Kernel PR 也能冒烟**，且不改脚本。
- 依赖：`qemu-system-x86`、`ovmf`；`SMOKE_TIMEOUT=120`；外层再设 job timeout 20。
- **不要** `timeout 120 ./smoke-boot.sh` 包一层还指错路径。

lint：可选、warn-only；行数阈值与仓库 **300** 对齐或标明「历史 800 仅警告」；find 含 `Core/`、`HAL/`、`Include/`、`Common/`。

### 第 3 刀（阶段 3）细则 — tag 发 Release

新 **`ToyKernel/.github/workflows/release.yml`**：

- `on.push.tags: ['v*']`
- `permissions.contents: write`
- 同一 runner 依次 `./build.sh` / `arm64` / `riscv`（先 clone lwIP）
- 打包仓内：`Kernel-*.elf` + `README.md` + `Documents/`（**不要**假设 `ToyKernel/` 前缀）
- `softprops/action-gh-release` + `GITHUB_TOKEN`
- job timeout 25

不把 ToyImage 整盘打进 Release（体积/密钥无关；Image 是另一仓）。

---

## 四、明确不做

- 改 Makefile / `build.sh` / smoke / 内核  
- 往 edk2 `.github` 加 job  
- 部署到自有服务器  
- 一次合三阶段  
- 把 smoke 硬塞进「只有 Kernel、没有 Image 资产」的 job 还用错路径  
- 构建超时 > 30 分钟  

---

## 五、测试口令（阶段 1 后）

1. 把 `PR-CI-1` 推进 `tanlaoshi/ToyKernel` 的 `main` 或开 PR。  
2. GitHub → Actions → **build** 绿。  
3. 下载 artifact `Kernel-x86_64`。  

阶段 2：Actions 见三个 Build + Smoke。  
阶段 3：`git tag v0.x.0 && git push origin v0.x.0`（**ToyKernel**）。

### 每刀报告模板

```
# 第 N 刀完成报告
## 改动文件
## Actions 结果（URL / ✅❌）
## 产物
## 下一步（等待确认）
```

---

## 六、结论摘要

1. **结构**：三仓；workflow 写在 **ToyKernel / ToyImage** 仓根 `.github/workflows/`。  
2. **build.sh**：仓根 `./build.sh`；注意 **lwIP 不在 git**。  
3. **smoke**：`Scripts/smoke-boot.sh`，依赖 Image + Kernel ELF + QEMU/OVMF。  
4. **已有** PR-Q1 `build.yml` + `smoke.yml`，阶段 1 是增强不是空白。  
5. **无 .gitee**。

**下一步**：本刀改完 `build.yml` 后 **TG**；**TS** 推 `main` 或开 PR 看 Actions。勿一次做 CI-2/3。
