# 任务：第二轮拆分 XhciCore.c（PR-H-xhci-core）

> **规格同 [`大文件拆分.md`](大文件拆分.md) / 首轮 xhci-split**：只搬家、不改逻辑；每步 build + smoke；真机相关先验键鼠。  
> **排期**：路线图 ★ **先于 PR-H-msc-6**（见 [`路线图.md`](路线图.md) 文首）。  
> **统计时点**：2026-09-13；`XhciCore.c` ≈ **1577** 行（A1/A2 已抽 MSC 函数 + `ProcessEvents*`）。

---

## 一、相对初稿的冲突与修订

初稿（`.ini` 思路）与**现况 / 血泪约束**对照：

| 初稿项 | 现况 | 本计划 |
| --- | --- | --- |
| 新建 `XhciMsc.c`（BringUp/Ready/Bulk） | **已有** `XhciMsc.c`（**PR-H-xhci-msc-split-1**） | **不做**；MSC 函数已迁 |
| `ProcessEvents*` → `XhciRing.c` | **已在** `XhciEvent.c`（**msc-split-2**） | **不做**；勿再搬 |
| 全局按文件重分配（环/命令/传输/键盘/MSC） | BSS 跨 `.c` 曾漂 HID DMA → EnableSlot/鼠假死 | **禁止搬全局**；定义仍留 `XhciCore.c`，`XhciInternal.h` extern |
| Makefile 手列源文件 | 已 `wildcard Drivers/XHCI/*.c` | **不必改**；新 `.c` 自动编入 |
| Core ≤600 / 新文件 ≤500 | 目标仍成立；`XhciInit` ~452 行本轮**可留 Core** | Init 再拆另开刀 |

**硬约束（继承 msc-split / WaitCommand）**

1. **只搬函数**；MSC/Bulk/HID **DMA 环与相关全局**不跨文件。  
2. `WaitCommand` 期间 `XhciIrq` / `XhciDrainEvents` 勿并发 `ProcessEvents`。  
3. 不与 **msc-6…** 功能刀同 PR。  
4. 每步先验 QEMU 键鼠；NUC 在 Controller / Keyboard / Device 步加强。

---

## 二、当前 XhciCore.c 函数行数（约）

| 行数 | 符号 | 拟归入 |
| ---: | --- | --- |
| 3–20 | `Read/WriteMmio*`、`Fence`、`FlushDma`、`Zero/CopyMemory`、`PointerToPhysical`、`ReadTsc`、`StallMs`、`Wait*Ms`、`MapXhciDma` | **Mmio** |
| 11–18 | `InitRing` / `Enqueue` / `TrbType` / `RingDoorbell` / `DcbaaSet` / `DcbaaFlush` | **Ring** |
| ~40 | `ResolveFwCmdRing` | **Ring**（或随 Command；本表放 Ring） |
| 33–62 | `WaitCommand` / `RecoverCommandRing` / `Command` | **Command** |
| 26–29 | `ServiceHidCompletions` / `WaitTransfer` | **Transfer** |
| 10–178 | `TakeLegacy` / `Halt*` / `ResetController` / `BootMarkRs` / `StartController` | **Controller** |
| ~76 | `SetupHidDevice` | **Device**（增强已有 `XhciDevice.c`） |
| 11–16 | `KbdPush` / `XhciKeyboardSetLeds` / `XhciDequeueKeyboard` | **Keyboard** |
| ~452 | `XhciInit` | **留 Core** |
| 3–11 | `XhciHidKeyboardReady` / `XhciAbandonNoHid` | **留 Core** |

全局定义（`gCmdRing` / `gEvtRing` / `gMsc*` / `gMouseIntrRing` / `gKbdQ` 等）→ **全部仍定义在 Core**。

---

## 三、目标树（第二轮后）

```
HAL/X64/Drivers/XHCI/
├── XhciInternal.h
├── XhciCore.c           # 全局定义 + XhciInit + Ready/Abandon
├── XhciMmio.c           # NEW
├── XhciRing.c           # NEW（不含 ProcessEvents）
├── XhciCommand.c        # NEW
├── XhciTransfer.c       # NEW
├── XhciController.c     # NEW
├── XhciKeyboard.c       # NEW
├── XhciEvent.c          # ✅ 已有 ProcessEvents*
├── XhciMsc.c            # ✅ 已有 MSC 函数；全局仍 Core
├── XhciDevice.c         # + SetupHidDevice
└── …
```

---

## 四、分 PR（插入 msc-6 之前）

| 序 | PR | 内容 | 故意不做 | 验收 | 状态 |
| -- | -- | ---- | -------- | ---- | --- |
| 1 | **PR-H-xhci-core-split-1** | `XhciMmio.c`：MMIO/内存/Stall/Wait/MapDma | 不搬全局；不动环/命令 | build + smoke；键鼠 | ✅ `40f88a8` |
| 2 | **PR-H-xhci-core-split-2** | `XhciRing.c`：Init/Enqueue/Doorbell/Dcbaa/`ResolveFwCmdRing` | 不搬 `ProcessEvents*`；不搬环缓冲全局 | 同上 | ✅ `4f4df06` |
| 3 | **PR-H-xhci-core-split-3** | `XhciCommand.c`：`Command` / `Recover` / `WaitCommand` | 不改 Wait 独占事件环语义 | 同上；claim 路径勿回归 | ⬜ ← **JX** |
| 4 | **PR-H-xhci-core-split-4** | `XhciTransfer.c`：`WaitTransfer` / `ServiceHidCompletions` | 不搬 xfer 全局 | 键鼠 IN | ⬜ |
| 5 | **PR-H-xhci-core-split-5** | `XhciController.c`：Reset/Halt/Start/TakeLegacy/BootMarkRs | **不**抽 `XhciInit` | QEMU + 建议 NUC 键鼠 | ⬜ |
| 6 | **PR-H-xhci-core-split-6** | `XhciKeyboard.c` + `SetupHidDevice`→`XhciDevice.c` | 不搬 kbd 队列全局 | QEMU + NUC 键鼠 | ⬜ |

**本柱完成后** → 路线图 ★ 回到 **PR-H-msc-6**。

### 4.1 内部头

`XhciInternal.h` 为迁出函数补声明（多数已有）；新增文件 `#include "XhciInternal.h"`，与现有 `XhciMsc`/`XhciEvent` 同模式。

### 4.2 依赖顺序

`Mmio` → `Ring` → `Command` → `Transfer` → `Controller` → `Keyboard`+`Device`。  
勿并行大挪，避免半截符号。

---

## 五、验收（每 PR）

- [ ] `./build.sh`（x64）通过  
- [ ] `ToyImage/smoke-boot.sh` 通过（含 xhci irq=msi / keyboard 行）  
- [ ] 无行为改动意图（纯 move）  
- [ ] **未**把 MSC/HID DMA 全局改到别的 `.c`  
- [ ] Controller / Keyboard / Device 步：NUC 键鼠点验（有条件）  
- [ ] 路线图：完成后从前半删本 PR，文末【归档】；本文表标 ✅  

---

## 六、修订记录

| 日期 | 说明 |
| --- | --- |
| 2026-09-13 | 初稿：七文件 + 全局重分配（分析待确认） |
| 2026-09-13 | 修订为 **6 刀 PR**；对齐已落地 `XhciMsc`/`XhciEvent`；**禁止 BSS 搬迁**；插入路线图 ★（msc-6 之前） |
| 2026-09-13 | **PR-H-xhci-core-split-1**：`40f88a8` — `XhciMmio.c`；`XhciCore.c` ~1410；build+smoke 过 |
| 2026-09-13 | TG：xhci-core-split-1 → ToyImage Kernel sync |
| 2026-09-13 | **PR-H-xhci-core-split-2**：`4f4df06` — `XhciRing.c`；`XhciCore.c` ~1302；build+smoke 过 |
| 2026-09-13 | TG：xhci-core-split-2 → ToyImage Kernel sync |
