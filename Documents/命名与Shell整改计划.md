# 命名与 Shell 整改计划（详细）

> 合并两份清单的**可执行计划**：  
> - 用户可见：[`Shell命令.md`](Shell命令.md)（一级/二级 + 别名，自然语序）  
> - 源码标识符：[`命名整改.md`](命名整改.md)（EDK2 风格写全词）  
> 路线图入口：目标 **25（1.3csh / C1～C3）**、穿插 **PR-R-name**；R1～R5 已收官。

**总原则**

| # | 约定 |
|---|------|
| 1 | **两条轨不要混在同一 PR**：Shell 改的是「用户敲的字」；命名改的是「C 符号 / 文件名」。 |
| 2 | 同一 PR **禁止**「大改名 + 改行为」。 |
| 3 | Shell 正统：**空格分一级/二级、无中横线**；旧粘连名只做别名。 |
| 4 | C 标识符：PascalCase 全词；**白名单缩写**（FAT/ELF/GOP/…）不写全成笑话。 |
| 5 | 每刀必过：`./build.sh`（至少 x86）；相关冒烟 `smoke-boot.sh` / virt headless；改到的讲义/结构说明同步。 |
| 6 | FAT 上 `HELLO.ELF` 等 **8.3 名不动**；脚本 kebab-case **默认不动**。 |

**建议总序（课堂价值优先）**

```text
D-res 收尾（若未归档）
  → C1 → C2 → C3          # Shell：用户天天敲
  → R6 → R7 → R8          # 命名：Hal / Common 导出 / 模块字符串
  → R9+ 穿插              # 文件名、static、可选目录
  ∥ V3 / DNS / 大锁 / 热分辨率（不堵 C/R）
```

---

## 甲轨：Shell（目标 25）

对照表权威：[`Shell命令.md`](Shell命令.md)。  
涉及主文件：`Include/Console.h`、`Common/Services/Console.c`、`ShellCommands.c`、`ShellCommandsFs.c`、`Db.c`（注册处）、virt 最小集注册。

### 阶段 S0 — 文档冻结（已基本完成）

| 项 | 状态 | 说明 |
|----|------|------|
| 正统/别名总表 | ✅ | 见 Shell命令.md §2 |
| 语序原则 | ✅ | 动词前 / 协议族对象前 |
| 与命名整改边界 | ✅ | 本文 |

验收：表无中横线正统名；每条有「现今 → 正统 → 别名」。

---

### PR-C1 — Console 一/二级分发 + 别名（🔧 机制落地；全量改挂见 C2）

**目标**：机制就位；可先只挂 2～3 个样例命令证明，不全量改挂。

| 交付 | 细节 |
|------|------|
| 数据结构 | 一级条目：Name、Help、Handler 或「子命令表」；子命令：Name、Help、Handler；别名：Alias → (一级[,二级]) |
| API | `ConsoleRegister`（仅一级）；`ConsoleRegister2(L1, L2, Help, Handler)`；`ConsoleRegisterAlias(CanonicalL1, Alias)`；`ConsoleRegisterAliasLine(Alias, L1, L2)` |
| 分发 | `RunLine`：别名展开 → 若 L1 有二级表则吃 `Argv[1]` → 否则仅 L1；缺二级打 usage |
| `help` | 按一级分组；二级缩进；括号列别名；可附中文短释（Locale 或 Help 字符串） |
| `CMD_MAX` | 别名**不**各占满额槽；挂在条目上 |
| 样例挂接（最小） | 例如 `tcp listen` + 别名 `tcplisten`；`halt` + `exit`/`quit`（已有可迁入新机制） |

**触及文件**：`Console.c` / `Console.h`；可选极小改 `ShellCommands.c` 做样例。

**验收**

```text
help                    # 见分组
tcp                     # usage: tcp <listen|connect|status> ...
tcp listen 9000
tcplisten 9000          # 同效
halt / exit / quit
```

**风险**：`list` 既是「列目录」又是 `list tasks` 的一级——分发规则必须是「有二级登记则优先吃第二词」，无二级才把剩余当路径参数（`list Apps`）。

**不做**：改完所有命令（留给 C2）；改 C 函数名 `CommandTcpListen`。

---

### PR-C2 — 命令改挂（按族分批，可 1 个 PR 或 2 刀）⭐

**目标**：[`Shell命令.md`](Shell命令.md) §2.1～2.8 全部正统化；现今名全部成别名。

建议批内顺序（降低 `list`/`show`/`make`/`remove` 冲突调试成本）：

| 批次 | 内容 | 正统示例 | 别名示例 |
|------|------|----------|----------|
| C2a | 内置 + 电源 | `help` `clear` `echo` `reboot` `halt` | `cls` `exit` `quit` |
| C2b | `list` / `show` / `test` / `run` / `set` 族 | `list` `list tasks` `list devices` `list volumes` `show memory` `show network` `show info` `show file` `test memory` `test glyph` `run user` `set language` | `ls` `ps` `lsdev` `vols` `mem` `net` `info` `filestat` `memtest` `zh` `runuser` `lang` |
| C2c | 文件动词族 | `print` `write` `write big` `make directory` `remove` `remove directory` `move` `sync file` `stress directory` | `cat` `wrbig` `mkdir` `rm` `rmdir` `mv` `filesync` `dirstress` |
| C2d | 网络 + DB + 杂项 | `udp *` `tcp *` `lwip *` `database *` `execute` `kill` `shell` `settings` `files` `edit` `font` `ping` | `udplisten`… `dbget`… `exec` |

**Handler 参数约定**：二级命令的 Handler 收到的 `Argv` 建议 **已去掉一级（及二级）**，或文档规定「从 Argv[2] 起是参数」——C1 定一种，C2 全遵守。

**同步文档**：`路线图` 归档验证命令、`当前进展`、`结构说明` 里出现的 `ps`/`mkdir`/`tcplisten` 等改为「正统（别名）」；`教学内容/` 函数手册若写了 Shell 用法一并改。

**验收**：Shell命令.md §5 整段可跑；旧名抽样 10 条仍可用。

**与命名整改交叉**：本 PR **只改注册字符串与 help**，不强制改 `CommandMem`→`CommandMemory`（可顺手，属 R 轨 P2）。

---

### PR-C3 — `store` 子命令对齐（⬜）

| 项 | 做法 |
|----|------|
| 正统 | `store list` / `install` / `remove` / `combo` / `uncombo` / `installed` / `sync` / `fetch` / `repo` |
| 别名 | 无参 `store`→`store list`；`store status`→`store list`；`store rm`→`store remove`；`store list-installed`→`store installed` |
| 代码 | `CommandStore` 内 strcmp 子词表；去掉对 `list-installed` 中横线正统依赖 |

**验收**：`store installed`、`store list-installed`、`store` 三者行为符合表；M1/M2 依赖/combo 回归。

---

### Shell 轨完成后的归档

- 路线图：C1～C3 ✅ → 文末归档；目标 25 收官。  
- `Shell命令.md` 顶栏改「已落地」；「现今名」列可改为「历史别名」或删「现今」只留别名。

---

## 乙轨：C 标识符命名（穿插 PR-R-name）

对照权威：[`命名整改.md`](命名整改.md)。  
**已完成**：R1～R5、`Drv`→`Driver`、PR-A15 `HalFrame*`、`Fs*`→`FileSystem*`（清单称 R5）、`Init*`→`Initialize*`（清单称 R5）——动手前用 `rg` 核对仓库实况，避免重复改。

### 与 Shell 的对应关系（勿混 PR，但用词对齐）

| Shell 正统（用户） | 源码侧宜对齐的方向（命名轨） |
|--------------------|------------------------------|
| `show memory` / `list tasks` | 模块日志 `"memory"` / `"scheduler"`；`CommandMemory`（P2） |
| `list` / `make directory` | 已是 `FileSystem*`；Handler 可 `CommandListDirectory`（P2） |
| `tcp listen` | 保持 `CommandTcpListen` 或改为 `CommandTcpListen` 已够全 |
| `database get` | `CommandDatabaseGet`（P2） |
| `show network` | 模块 `"network"`（可选） |

---

### PR-R6 — Hal 公开 API 补全词（P0 剩余）（⬜）

| 范围 | 示例（详见命名整改 §2.3） |
|------|---------------------------|
| CPU/SMP | `HalCpuId`→`HalGetCpuId`；`HalCpuTickInc`→`HalCpuIncrementTicks`；`HalSmpStartAps`→…（或白名单 Ap） |
| 页 | `HalPageIsCow`→`HalPageIsCopyOnWrite`；宏 `HAL_*_COW`→`COPY_ON_WRITE` |
| Net | `HalNetGetMac`→`HalNetGetMacAddress`；`HalNetGetIp`→`HalNetGetIpAddress` |
| 其它 | `HalSerialHexFormat`→`HalSerialFormatHex`；`HalUserInstall`→`HalInstallUserMode` |

**要求**：三 arch 同改；`Include/hal*.h` 与实现一起；**不改** Shell 字符串。

**验收**：`./build.sh`、`./build.sh arm64`、`./build.sh riscv`；`smoke-boot.sh`；virt 无头冒烟可选。

---

### PR-R7 — Common 导出与 Theme/Gui/Console 短名（P1）（⬜）

先 `rg` 确认是否仍有旧名：

| 族 | 示例 |
|----|------|
| Theme | `ThemeDesktopBg`→`ThemeDesktopBackground` |
| Gui | `GuiSetFocusWin`→`GuiSetFocusWindow` |
| Console | `ConsoleHex32`→`ConsoleWriteHex32` |
| 全局 | `gSchedOnline`→`gSchedulerOnline`（若仍短） |

可与 R6 分开，避免 Diff 过大。

---

### PR-R8 — 模块表字符串（P1，课堂观察点）（⬜）

`gModules[]` 等日志短名：

| 现存 | 建议 |
|------|------|
| `"mem"` | `"memory"` |
| `"vmm"` | `"virtual-memory"` 或 `"VirtualMemory"`（选定一种） |
| `"fs"` | `"file-system"` |
| `"sched"` | `"scheduler"` |
| `"net"` | `"network"`（可选） |

**必须**同步改讲义里「看串口 `[mod] mem`」类句子。  
可与 Shell C2 收尾后做，便于课堂同一周讲「用户敲 show memory / 日志打 memory」。

---

### PR-R9 — 类型 / 宏补全（P1～P2）（⬜）

| 示例 | 建议 |
|------|------|
| `VM_ADDR_SPACE` | `VIRTUAL_ADDRESS_SPACE` |
| `FAT_DIR_ENT` | `FAT_DIRECTORY_ENTRY` |
| `CPU_RUNQ` | `CPU_RUN_QUEUE` |
| `HAL_FRAME` / `INT_FRAME` | 统一一个全名 |

体积大，宜单独 PR；不与 R6 合并。

---

### PR-R10+ — 文件名与可选目录（P1～P2，低优先）（⬜）

| 项 | 建议 | 备注 |
|----|------|------|
| `Page.c`→`PageTable.c` | 三 arch | 改 Makefile/链接 |
| `Isr.S`→`Interrupt.S` | X64 | |
| `VirtioBlk`→`VirtioBlock` | 文件名 | API 若已是 Block 则只改文件 |
| `HAL/RiscV`→`RiscV64` | 可选 | 成本高，单独评估 |
| `User/include/toy_syscall.h`→`ToySyscall.h` | P2 | |
| `StubHelloBlob.c` | 去 Stub | P2 |

**默认不动**：`build.sh`、`run-*.sh`、`crt0.S`、`HELLO.ELF`。

---

### PR-R-p2 顺手刀（不单开除非攒批）

- `CommandMem`→`CommandMemory`（可跟 C2）  
- `MemCopy`/`MemZero`→`CopyMem`/`ZeroMem`（局部）  
- `gScreenW`/`gWins`→`gScreenWidth`/`gWindows`

---

## 丙轨：与命名/Shell 无关但同季的体验债

| PR | 说明 | 与甲乙关系 |
|----|------|------------|
| **D-res** | 分辨率 THEME/rootfs/stash | 可先于或并行 C1；不改命令名 |
| **N-dns** | DNS / socket 错误码 | 不堵 |
| **S-lock** | 调度大锁 | 不堵 |
| **G-hotres** | 热分辨率 | 宜 D-res 后 |
| **V3** | 工具链调研文档 | 他路 |

---

## 里程碑与估时（人天量级，供排期）

| 里程碑 | 含 | 约 |
|--------|----|----|
| M1 | C1 机制 + 样例 | 1～2 d |
| M2 | C2 全量改挂 + 文档 | 2～3 d |
| M3 | C3 store | 0.5 d |
| M4 | R6 Hal API | 1～2 d |
| M5 | R7+R8 Common + 模块字符串 | 1～2 d |
| M6 | R9 / R10 按需 | 穿插 |

---

## 每 PR 检查清单（复制到描述里）

```text
[ ] 只做命名或只做 Shell，不夹带功能
[ ] rg 旧符号 / 旧命令字符串为零（或仅出现在「别名/历史」文档）
[ ] ./build.sh（x86）；若动 Hal/模块则三 arch
[ ] smoke-boot.sh 或手工：help + 3 条正统 + 3 条别名
[ ] 结构说明 / 教学内容 / 路线图验证块已改
[ ] Shell 轨：正统无 '-'；别名表与 Shell命令.md 一致
[ ] 命名轨：未误伤白名单缩写
```

---

## 文档维护

| 文档 | 职责 |
|------|------|
| [`Shell命令.md`](Shell命令.md) | 用户命令正统/别名唯一表 |
| [`命名整改.md`](命名整改.md) | C/文件/模块符号清单与白名单 |
| **本文** | 两轨合并的**顺序、PR 边界、验收** |
| [`路线图.md`](路线图.md) | 状态勾选与归档 |

更新约定：Shell 表改用词 → 先改 Shell命令.md，再改代码；命名清单勾掉已做项时在命名整改.md 标 ✅，并在路线图文末归档对应 PR。
