# ToyOS 模块化与 App 课堂闭环

> **状态**：**柱已升星**；MOD-0 规格齐；★ = [`PR-MOD-app-verify`](../路线图.md#pr-mod-app-verify)（bundle 冒烟）。  
> **定位**：目录/文件结构模块化 + 驱动一设备一目录 + App「开发→打包→安装」课堂叙事。  
> **相关**：[`可替换模块化架构规划.md`](../已完/可替换模块化架构规划.md)（SCHED/MEM/FS **政策**可替换 · 已 GD）· [`应用资源自包含与字体共享.md`](../已完/应用资源自包含与字体共享.md)（目录包代码 · 已 GD）· [`开发/应用开发指南.md`](../开发/应用开发指南.md) · [`驱动/驱动开发指南.md`](../驱动/驱动开发指南.md) · [`路线图.md`](../路线图.md)  
> **命名**：PascalCase；新 `.c` ≤300；搬家刀不改行为。

---

## 〇、与已有柱的边界

| 柱 | 解决什么 | 本柱是否重做 |
| -- | -------- | ------------ |
| 可替换模块化（SCHED/MEM/FS） | `*_OPS` + Makefile 选实现 | **否**（不重开） |
| 设备管理器 / 驱动匹配 | 枚举、Claim、友好名 | **否** |
| 应用目录包（PR-S-bundle） | `Apps/<id>/` 安装/字体/桌面/卸 | **否**（代码已齐；本柱补**课堂路径文档与样例**） |
| **本柱** | **同模块同目录**；**一设备一文件夹**；**开发→打包→安装**讲清楚 | — |

---

## 一、目标与硬约束

### 目标

1. **大分层清晰**：`HAL` / `Common/Modules` / `Common/Services` / `Common/Library` / `Core` 职责不变。
2. **层内模块化**：同一模块的 `.c/.h` 落在同一目录（例外见下）。
3. **驱动**：新增设备 = 新增文件夹；禁止再往 `HAL/*/Drivers/` **根**丢业务实现。
4. **App 课堂**：从写代码到 Store 安装有一条可抄的正统路径（目录包）；RootFs 根 ELF 收敛为白名单，产品/课包经 catalog 注册。

### 硬约束

| # | 约束 |
| - | ---- |
| 1 | **不改**对外 ABI（`Vfs*` / `Store*` / `Hal*` / `Fat*` 等公开头签名） |
| 2 | 搬家刀 = 只迁路径 + Makefile / `#include`；行为与现网一致 |
| 3 | 每刀独立可三架构 `./build.sh`；涉及桌面/引导则 `smoke-boot` |
| 4 | 新 `.c` ≤300；已瘦文件不借机揉功能 |
| 5 | `HAL/Virt/`（MMIO virtio）与 `HAL/X64/Drivers`（PCI）**双家保留**，不强行合并 |
| 6 | App 正统路径 = Store **目录包** → `Apps/<id>/`；根 `exec` / 扁平 `Apps/*.ELF` = 捷径/遗留 |
| 7 | 实现刀须认文首 ★；本柱已升星，见路线图排队 |
| 8 | 一次一刀；不与 iwl/xhci 长函数刀同文件打架 |

---

## 二、目录约定（谁住哪）

| 层 | 路径 | 住什么 | 不住什么 |
| -- | ---- | ------ | -------- |
| 政策可替换 | `Common/Modules/<Name>/` | SCHED / MEM / FS 默认实现 | 硬件 MMIO、GUI |
| OS/UI 服务 | `Common/Services/<Name>/` | Shell、Desktop、Store、Tcp、Theme… | PCI/USB 寄存器 |
| 共享库 | `Common/Library/` | Elf、UI、Vfs、Block 抽象 | 具体网卡驱动 |
| 框架 | `Core/` | Device 表、Module runner、调度框架壳 | 设备私有探测细节 |
| 硬件 | `HAL/<Arch>/Drivers/<Device>/` | 一设备一目录的实现 | Common `#include` 驱动私头 |
| Virt 共享 | `HAL/Virt/` | 跨 arch MMIO virtio / DTB / ramfb | X64 专用 PCI 细节 |
| 学生替换 | `Student/` | 课设模板（默认不链） | 上游默认路径 |

**Services 根散落（轨 A 债，盘点 2026-09-30）**

- `LwIp.c` / `LwIpConfig.c` / `LwIpDhcp.c` / `LwIpSocket.c`、`NetConfig.c`、`Udp.c`、`Install.c`
- 部分 `Gui*.c`（Compose/Cursor/Fade/Hit/Wm…）仍在 Services 根，多数 Gui* 已有子目录——归拢时优先并入既有 `Gui*` 目录，勿新造平行树。

**Drivers 根散落（轨 B 债）**

- Input：`InputXhci*.c`、`InputEhci.c`、`InputUhci.c`、`InputPs2.c`
- Block：`BlockAhci.c`、`BlockAta.c`、`BlockNvme.c`、`BlockMsc.c`、`UsbMsc.c`、`Ata.c`
- 其它：`Serial.c`、`DemoDriver.c`（Demo 可保持扁平或进 `_template` 旁，规范刀写死）
- Net L2 胶水：`Drivers/Net/NetE1000.c`、`NetIwl.c`、`NetVirtio*.c`、`NetAlx.c`、`NetRtl.c`…（协议核 `Net.c` / `NetArp.c` 等去留见轨 B）

---

## 三、三轨总览

```text
大分层 (HAL / Modules / Services / Library)
    │
    ├─ 轨 A  同模块同目录（Services/Modules 归拢）
    ├─ 轨 B  一设备一文件夹（Drivers 根债清掉）
    └─ 轨 C  App 开发 → 打包 → 安装（文档 + 自包含样例）
```

| 轨 | 一句话 | 代码改动量 | 课堂价值 |
| -- | ------ | ---------- | -------- |
| A | Services 散文件进子目录 | 中（搬家） | 中（维护） |
| B | 驱动根散落进设备目录 | 中高（搬家+冒烟） | 高（加驱动课） |
| C | 指南 + 样例自包含 + **RootFs/商店债收敛** | 中（文档/资源/`build.sh`） | **最高** |

**默认执行顺序（建议）**

`PR-MOD-0` → `app-doc` → `app-inventory` → `app-sample` → `app-repack` → `rootfs-trim` → `app-verify` → `drv-norm` → `drv-input` → `drv-block` → `drv-netglue` → `svc-lwip` → `svc-misc`

（先文档与 RootFs/商店债，再驱动搬家；避开与 F′ iwl/xhci 同文件并行。）

---

## 四、轨 A · 分层内同模块同目录

### 4.1 现状

- `Modules/` 仅 SCHED/MEM/FS，目录形态已达标。
- `Services/` 大量已是 `Theme/`、`Tcp/`、`Store/`…；**LwIp / Udp / NetConfig / Install** 与部分 **Gui\*** 仍在根。

### 4.2 PR 表

| PR | 交付 | 估工 | 依赖 | 验收 |
| -- | ---- | ---- | ---- | ---- |
| **PR-MOD-0** | 本规格 + 路线图候补指针 + 本节目录约定 | **0.5～1d** | 无 | 仅文档；★ 不占 |
| **PR-MOD-svc-lwip** | `LwIp*.c` + `NetConfig.c` + `Udp.c` → `Services/LwIp/`（`Install.c` 若仅 Store 胶水则进 `Store/` 或保持根并在刀内说明） | **1～1.5d** | MOD-0 | 三架构 build；既有 net smoke / 手工 ping 不回归 |
| **PR-MOD-svc-misc** | 其余 Services 根散落（含 Gui 根文件）一次 ≤3 逻辑模块归夹 | **1d** | MOD-0 | 同上 + `smoke-boot` |

小计 A：**2.5～3.5d**（MOD-0 必做；后两刀可选排队）。

---

## 五、轨 B · 驱动一设备一文件夹

### 5.1 布局规则（写死）

1. **新设备**：必须 `HAL/<Arch>/Drivers/<DeviceName>/`，禁止新增根级业务 `.c`。
2. **类驱动 vs 控制器**：HID/MSC 等若强绑定某宿主控制器，**优先并入该控制器目录**（例：`InputXhci*` → `XHCI/`），避免再建平行 `Input/` 大杂院。
3. **Net L2 胶水**：`NetE1000.c` 等迁入对应设备目录（如 `E1000/NetGlue.c` 或保留原名）；`Drivers/Net/Net.c` 等**协议核**可留在 `Net/`（规格刀 `drv-netglue` 列清单）。
4. **Virt**：MMIO 继续 `HAL/Virt/`；X64 PCI virtio-net 胶水跟 X64 设备目录，**不**并进 Virt。
5. **头文件**：设备私头进设备目录；公开 `Include/` 门面不搬（除非已是私头误放根）。

### 5.2 现状债（X64）

| 簇 | 根/旁路文件 | 目标（原则） |
| -- | ----------- | ------------ |
| Input | `InputXhci*.c` → `XHCI/`；`InputEhci.c` → `Ehci/`；`InputUhci.c` → `Uhci/`；`InputPs2.c` → `Ps2/` | 并入控制器目录 |
| Block | `BlockAhci.c` → `Ahci/`；`BlockNvme.c` → `Nvme/`；`BlockAta.c`/`Ata.c` → ATA 目录；`BlockMsc.c`/`UsbMsc.c` → 与 MSC/XHCI 归属一致（刀内写死） | 一设备一夹 |
| Net glue | `Net/NetE1000.c` 等 | 进 `E1000/`/`Iwl/`/… |
| Demo/Serial | `DemoDriver.c`、`Serial.c` | norm 刀定：Demo 近 `_template`；Serial 独立 `Serial/` |

### 5.3 PR 表

| PR | 交付 | 估工 | 依赖 | 验收 |
| -- | ---- | ---- | ---- | ---- |
| **PR-MOD-drv-norm** | 规则写入本文 §5.1 + 更新 [`驱动开发指南.md`](../驱动/驱动开发指南.md) + `HAL/X64/Drivers/_template/README.md` | **0.5d** | MOD-0 | 仅文档 |
| **PR-MOD-drv-input** | Input\* 按 §5.2 迁入控制器目录；改 Makefile / 引用 | **1.5～2d** | drv-norm | 三架构 build；`smoke-boot` 键鼠后端 PASS |
| **PR-MOD-drv-block** | Block\*/UsbMsc/Ata 迁入 | **1～1.5d** | drv-norm | build + 启动挂卷 / 列盘不回归 |
| **PR-MOD-drv-netglue** | 设备 L2 胶水迁入设备目录；协议核清单固化 | **1～1.5d** | drv-norm | build；QEMU virtio/e1000 ping 或既有 net 冒烟 |

小计 B：**4～5.5d**。可选 NUC 键鼠/盘/网手测 **+0.5～1d**。

---

## 六、轨 C · App 开发 → 打包 → 安装

### 6.1 三条部署路径（课堂必须讲清）

| 路径 | 步骤摘要 | 定位 |
| ---- | -------- | ---- |
| **A 捷径** | `User/Pkg` → `RootFs/X64/<NAME>.ELF` → `exec` | 5 分钟 Hello；**不**经 Store |
| **B 遗留** | 扁平 `Apps/*.ELF` + 菜单扫描 | TaskMgr 旧范例；新课少推 |
| **C 正统** | `Assets/Store/packages/<id>/` + `catalog.txt` → `store combo`/`install` → `Apps/<id>/` | **打包课默认** |

### 6.2 正统路径（C）编号步骤（供指南抄入）

1. 开发：`ToyKernel/User/Pkg`（或 SDK）编出 `MYAPP.ELF`。
2. 建包目录：`Assets/Store/packages/<id>/`  
   - `PKG.TXT`（`id=` `type=app` `file=` `title=`；可选 `desktop=`/`taskbar=`/`icon=`/`font=`/`depends=`）  
   - 载荷 ELF（与 `file=` 同名，建议 **包内自包含**）  
   - 可选 `Assets/`（图标、私有字体、数据）
3. 目录行：`Assets/Store/catalog.txt` 增加  
   `id|type|version|file|sha256|arch|title[|depends]`
4. 同步 rootfs：`ToyImage` 侧 `prepare-rootfs` / 构建脚本已有拷贝约定。
5. Guest：`store combo <id>` 或先装依赖再 `store install <id>`。
6. 验证：`Apps/<id>/` 存在；`exec Apps/<id>/<ELF>` 或桌面/开始菜单（需 `taskbar=yes` 等）。
7. 卸：`store uncombo <id>` / `store remove <id>` → 树删 + DB 清。

**载荷解析顺序**（实现已有，文档须写）：`StoreCache/<file>` → `packages/<id>/<file>` → 卷根 `<file>`。

### 6.3 样例债（包内）

| id | 现状 | 目标 |
| -- | ---- | ---- |
| `guidemo` | ✅ 包内 `GUIDEMO.ELF`+PKG+Assets（sample） | 保持自包含；扁平根可后续 trim |
| `hello` | ✅ `packages/hello/` PKG+ELF（sample） | 同上 |
| `cat` | ✅ `packages/cat/` PKG+ELF（sample）；扁平 `Apps/CAT.ELF` 仍在 | repack/trim 去扁平 |
| `sun8` / `demopack` | 字体/asset 较完整 | 作非 app 对照样例 |

### 6.4 RootFs 根目录 ELF 债（盘点 2026-09-30）

> 痛点：`build.sh` 把几乎全部 `User/*.elf` **无差别刷到** `RootFs/X64/*.ELF`；多数**未入** `catalog.txt`、**无**标准 `packages/<id>/`；`Apps/` 混用扁平 `*.ELF` 与目录包；学生分不清「演示快捷方式」与「商店安装态」。

**根上约 32 个 `*.ELF`（另 `Kernel.elf` / `LIBTOY.SO`）**

| 分类 | 成员（例） | 现状 | 处置原则 |
| ---- | ---------- | ---- | -------- |
| **已入 catalog** | `HELLO` `GUIDEMO` `CAT` | 根 +（部分）`Apps/<id>/` 或扁平 `Apps/`；包不完整 | 标准包自包含；根可留捷径或改只走 StoreCache |
| **产品/菜单向、未入店** | `TASKMGR` `SNAKE` `WINDEMO` `BLITDEMO`… | 仅根；TaskMgr 另扁平 `Apps/TASKMGR.ELF` | **优先入店**：`packages/<id>/` + catalog + `taskbar=`/`desktop=` 按课需 |
| **syscall/课烟雾、未入店** | `FORK` `PIPEDEMO` `THREADSMOKE` `PTHREADSMOKE` `ENOSYS` `MMAPDEMO`… | 仅根 `exec` | 二选一写死：**(1)** 白名单保留根捷径并文档标明；**(2)** 打成 `type=app` 教学包（可不进开始菜单） |
| **网/文件小工具** | `NETDEMO` `NETSRV` `NETLIB` `SOCKDEMO` `WRITE` `DIRDEMO` `CWDDEMO` `COUNT`… | 仅根 | 同烟雾：白名单或入店；废弃则停拷并删 RootFs 陈货 |

**Apps/ 混布（须收敛）**

| 路径 | 问题 |
| ---- | ---- |
| `Apps/hello/` `Apps/guidemo/` | 接近正统，但构建仍双写根 ELF |
| `Apps/TASKMGR.ELF` `Apps/CAT.ELF` | **扁平遗留**，非 `Apps/<id>/` |
| 无 PKG 的预装树 | 未体现「商店安装注册」（ToyDB `si.*`），与 `store install` 语义不一致 |

**构建侧根因**：[`ToyKernel/build.sh`](../../build.sh) x86 同步段（约 L144+）逐个 `cp` 到卷根；仅 hello/guidemo/taskmgr 额外进 `Apps*`/`StoreCache`。

### 6.5 策略（写死）

1. **正统交付物** = `Assets/Store/packages/<id>/` + `catalog.txt`；安装后 = `Apps/<id>/` + ToyDB。
2. **根 `*.ELF`**：仅保留**文档白名单**课用捷径（如 `HELLO.ELF`）；其余停止 `build.sh` 拷贝，并清理 RootFs 陈货。
3. **禁止新增**扁平 `Apps/*.ELF`；既有 `TASKMGR`/`CAT` 扁平在 repack 刀改为 `Apps/<id>/`。
4. **不**要求烟雾 demo 全部进开始菜单；入店者默认 `taskbar=no`，需展示再开。
5. 「过时」判定：源仍在 `User/` 且能编过 = 刷新包内 ELF；源已删或课不用 = 停同步并移出 RootFs（清单刀勾掉）。

### 6.6 PR 表（轨 C，含 RootFs）

| PR | 交付 | 估工 | 依赖 | 验收 |
| -- | ---- | ---- | ---- | ---- |
| **PR-MOD-app-doc** | [`应用开发指南.md`](../开发/应用开发指南.md) 增「打包与安装」；对齐 `Apps/readme`、`Assets/Store/README` 与 `StoreCache`；标明 A/B/C 与根白名单政策 | **1～1.5d** | MOD-0 | 文档可跟做 |
| **PR-MOD-app-inventory** | 全表：每个根 ELF → 入店 / 根白名单 / 废弃；写入本文附录或 `Assets/Store/ROOTFS-ELF.md`；与 `User/` 源一一对应 | **0.5d** | app-doc | 表无遗漏；确认后再搬家 |
| **PR-MOD-app-sample** | `packages/guidemo/` + `packages/hello/`（+`cat`）自包含 ELF+PKG | **1d** | inventory | 拷包 + catalog 可 combo |
| **PR-MOD-app-repack** | 白名单产品/课包入店（至少 `taskmgr`、`snake`；按 inventory 扩 3～8 个）；catalog 注册；扁平 `Apps/*.ELF` 改目录包 | **1.5～2.5d** | sample | `store combo` 可装；菜单/exec 路径正确 |
| **PR-MOD-rootfs-trim** | 改 `build.sh`（及 `prepare-rootfs` 若双写）：正式 app → `packages/`/`StoreCache`；根仅白名单；删 RootFs 非白名单陈货 | **1～1.5d** | repack | 新编后根 ELF 集合 = 白名单；smoke-boot 不挂 |
| **PR-MOD-app-verify** | `test-bundle-*.exp` + 抽测 repack id；指南手测清单；`store-disk` 真机挂账不阻塞 | **0.5～1d** | rootfs-trim | expect PASS |

小计 C（含 RootFs）：**5.5～8d**。

---

## 七、估工汇总

| 范围 | 人日 |
| ---- | ---- |
| **仅规格入库**（本文件 = MOD-0） | **0.5～1d** |
| **文档柱偏课堂**（MOD-0 + app-doc + inventory） | **2～3d** |
| **文档 + App/RootFs 闭环**（0 + 轨 C 全） | **6～9d** |
| **三轨做完**（A+B+C） | **12.5～18d**（约 **2.5～3.5 周**） |

人日口径：单人熟悉本仓；含写作与 QEMU 冒烟；**不含** NUC 真机长测（轨 B 可选另加）。

---

## 八、验收 / 非目标

### 本规格（MOD-0）验收

- [x] 本文入 `Documents/待做/`
- [x] 路线图文首候补 + `#sec-exec-plan` 挂指针（随同 PR）
- [x] ★ 仍空

### 整柱远期验收（各实现刀各自勾）

- [ ] 指南含可抄的打包安装节
- [ ] 至少一个自包含 app 样例包
- [ ] RootFs 根 ELF = 文档白名单；产品/课包经 catalog 可装
- [ ] 无新增扁平 `Apps/*.ELF`
- [ ] Drivers 根无新增业务 `.c`；Input/Block/Net 债按刀清完
- [ ] Services LwIp 等归夹完成

### 非目标

- 重做设备管理器 / 驱动匹配框架
- 合并 `HAL/Virt` 与 X64 PCI 驱动树
- 重开 SCHED/MEM/FS 政策可替换柱
- **强迫**全部烟雾 demo 进开始菜单（可入店但不显示）
- 本柱规格刀搬驱动或改 Store 安装语义（trim/repack 只改同步与包装）

---

## 九、修订

| 日期 | 说明 |
| ---- | ---- |
| 2026-09-30 | 初稿：三轨 + PR 表 + 估工；对应 Untitled-1 模块化愿景 |
| 2026-09-30 | 补 §6.4–6.6：RootFs 根 ELF 过时/未入店/未标准打包；增 inventory/repack/rootfs-trim；估工上调 |
| 2026-09-30 | 升星：★ = PR-MOD-app-doc；排队 inventory…verify |
| 2026-09-30 | inventory：ROOTFS-ELF.md 全表；★ 待 TG |
| 2026-09-30 | sample：hello/guidemo/cat 包内 ELF；build.sh PackStore；pack-app.sh |
| 2026-09-30 | repack：taskmgr/snake/windemo/blitdemo 入店；废扁平 Apps |
