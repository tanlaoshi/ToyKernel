# 任务：xHCI 事件环单消费者（PR-H-xhci-evt-excl）

> **规格性质**：改语义（竞态修复），**不是**大文件搬家。  
> **排期**：见 [`路线图.md`](路线图.md) 文首 ★。  
> **动机**：MSC claim 前 `XhciFallbackToPoll("msc-claim")`——dual 下 Drain/IRQ 与 `WaitCommand` 抢事件环，EnableSlot 完成丢失 → `cmd sick`。目标是严格单消费者后**去掉该让步**。

---

## 一、背景与失败链

当前会调用 `ProcessEvents*`（唯一推进 CCS/ERDP）的路径：

| 路径 | 文件（约） | 说明 |
| --- | --- | --- |
| `XhciIrq` | `XhciIrq.c` | MSI/IOAPIC |
| `XhciDrainEvents` | `XhciIrq.c` | dual/poll 备份；Input 轮询也会进 |
| `WaitCommand` | `XhciCommand.c` | 命令完成 |
| `WaitTransfer` | `XhciTransfer.c` | EP0 |
| `WaitBulk` | `XhciMsc.c` | MSC BOT |
| `StallMs` | `XhciMmio.c` | 忙等里顺带 Drain |
| `RecoverCommandRing` | `XhciCommand.c` | CA 后排空 ×64 |
| 若干 ad-hoc | Device/Hid 等 | Stop/Reset 后偶发 |

```
多路调用 ProcessEvents*
        │
        ▼
  CCS / ERDP 推进（无真正互斥）
```

| 机制 | 现状 |
| --- | --- |
| `gXhciCmdWaiting` | 仅**真机** `WaitCommand` 置位；门铃常在置位**前**响 |
| QEMU `WaitCommand` | **不**置位 |
| `WaitTransfer` / `WaitBulk` / `StallMs` | **无**独占 |
| dual 硬约束 | Drain backup **不得**因 `gUseIrq=1` 关掉 |
| claim 缓解 | 一律 `FallbackToPoll("msc-claim")` |

**失败链**：完成被 Drain/IRQ 吃掉 → `gCmdDone=0` 超时 → CA → `gXhciCmdSick` → 键鼠假死。

---

## 二、目标

命令 / 传输敏感窗口内：**唯一路径**调用 `ProcessEvents*` 推进环。  
HID 在 dual 下仍可靠：Wait 循环内继续 `ProcessEvents* + ServiceHidCompletions`（见下「采纳与否」）；Drain 在非独占时作 backup。

最终：claim **保持 dual**，日志无 `fallback msc-claim`。

---

## 三、硬约束

1. **不改**对外 `XHCI.h` API 语义（可增内部 `XhciInternal.h` 符号）。  
2. **不搬** `gEvt*` / `gCmd*` / MSC 环等 DMA/BSS 定义位置。  
3. **不关** Drain backup（dual 下仍是 HID 备份；仅在独占窗口跳过）。  
4. **不与** e1000 / GUI 混同一 PR。  
5. `Wait*` / 独占窗口内**勿持** `gHidQueueLock`（避免与 Wait 嵌套死锁）。  
6. 每个 PR 独立可编译、可 `smoke-boot`。  
7. **excl-3 无 NUC 不并**；翻车只回滚 3。

### 锁顺序（excl-2 起）

| 锁 | 用途 |
| --- | --- |
| `gEvtConsumerLock` | 仅保护事件环消费（`ProcessEvents*`） |
| `gHidQueueLock` | 仅保护键鼠队列 |

**约定**：两把锁**不要同时持有**。先消费事件环并释放 `gEvtConsumerLock`，再处理 HID 队列。  
勿在持 `gHidQueueLock` 时调 `ProcessEvents*`。

---

## 四、设计要点（从详稿采纳 / 修正）

### 采纳

| 点 | 说明 |
| --- | --- |
| 统一独占 API | `XhciEventEnterExclusive` / `Leave` / `IsExclusive`，放 `XhciEvent.c`；诊断 `gStatIrqSkipped` |
| 门铃在独占区内 | `Enqueue` → fence → `RingDoorbell` → **同窗**内再等完成（消灭门铃→置旗窗口） |
| excl-1 即覆盖全部 Wait* | `WaitCommand` / `WaitTransfer` / `WaitBulk` 一并进独占（不必拖到 excl-2） |
| Irq / Drain / StallMs 守门 | `IsExclusive()` 则跳过环推进；IRQ 可 `gStatIrqSkipped++` |
| Recover 临时独占 | 若尚未独占则 Enter，排空后 Leave |
| excl-2 消费锁 | `gEvtConsumerLock` + `ProcessEventsLocked` / 对外 `ProcessEvents*` 加锁；Irq/Drain 走 Locked |
| excl-3 去 fallback | 仅删 `XhciFallbackToPoll("msc-claim")`（及相关「为 claim 故意降 poll」逻辑） |

### 不采纳 / 修正

| 详稿项 | 处理 |
| --- | --- |
| Wait 中每 ~15ms `LeaveExclusive` 再 Enter | **不做**。窗口会再次允许 Drain/IRQ 抢完成，抵消单消费者。HID 靠独占区内 `ProcessEvents* + ServiceHidCompletions` 消化中断 TRB（与现 `gXhciCmdWaiting` 期行为同思路） |
| `Command` 先 Enter、门铃后 Leave、再交给 `WaitCommand` 独占 | **不做**。中间空隙仍丢完成。应 **门铃与等待同属一次独占窗** |
| 把大段伪代码当唯一实现 | 文档只定契约；实现对照现树微调，避免整文件粘贴式改写 |
| 「先分析再改代码」清单 | 执行 **JX=excl-1** 时按需对照代码；本文件为规格，不重复作业单 |

---

## 五、PR 拆分（1 → 2 → 3）

### PR-H-xhci-evt-excl-1：独占旗 + 门铃同窗 + 守门（✅ TG `7c28c51`）

**目标**：建立统一独占；所有 Wait* + 门铃同窗；Irq/Drain/StallMs/Recover 尊重旗；**仍保留** `fallback msc-claim`。

**主要文件**：`XhciEvent.c` / `XhciInternal.h` / `XhciCommand.c` / `XhciTransfer.c` / `XhciMsc.c` / `XhciIrq.c` / `XhciMmio.c`

**契约**：

```c
/* XhciEvent.c — 示意 */
void XhciEventEnterExclusive(void);  /* 置 gEvtExclusive；可与 gXhciCmdWaiting 对齐或取代其门控 */
void XhciEventLeaveExclusive(void);
int  XhciEventIsExclusive(void);
extern volatile UINT32 gStatIrqSkipped; /* IRQ 因独占跳过环推进的次数 */
```

- `Command`：Enter → 清 `gCmdDone` → Enqueue → doorbell → **同窗内**轮询 `ProcessEvents*` + `ServiceHidCompletions` 至完成或超时 → Leave。真机与 QEMU 皆然。  
- `WaitTransfer` / `WaitBulk`：同样 Enter…Leave 包裹整段等待（若门铃在更外层，须保证门铃不在无独占时响起；或把门铃一并收入该窗）。  
- `XhciIrq` / `XhciDrainEvents`：`IsExclusive()` 则**不**调 `ProcessEvents*`（IRQ 可仍清 IP / 计 skipped，按现树最小改）。  
- `StallMs`：仅当 `!IsExclusive()` 才 `ProcessEventsRealPc`。  
- `RecoverCommandRing`：排空前后保证独占。  
- **不删** `XhciFallbackToPoll("msc-claim")`。

**验收**：

```bash
cd ToyKernel && ./build.sh
cd ../ToyImage && ./smoke-boot.sh   # kbd + xhci irq=msi
```

- claim 路径**仍可**出现 `boot: xhci irq=poll (fallback) msc-claim`  
- 无新 `cmd sick`；键鼠不回归  
- PHOTO / 诊断可见 `gStatIrqSkipped`（有即可；空载不必苛求具体阈值）

---

### PR-H-xhci-evt-excl-2：事件环消费串行锁（✅ TG `e982dd6`）

**目标**：所有环推进走同一把 `gEvtConsumerLock`，消灭 TOCTOU（查旗与 ProcessEvents 之间被插入）。

**主要文件**：`XhciEvent.c` / `XhciIrq.c`（必要时 Internal）

**契约**：

- `ProcessEventsLocked`：无锁，仅环逻辑。  
- `ProcessEvents` / `ProcessEventsRealPc`：Lock → Locked → Unlock。  
- Wait 独占窗内可直接调 `ProcessEventsLocked`（已排除 Irq/Drain），或调带锁版本（Irq 已被旗挡住，通常不争用）。  
- `XhciIrq` / `XhciDrainEvents`：先 `IsExclusive` 早退；否则 Lock + Locked；**再**在锁外做 HID 入队（`gHidQueueLock`）。  
- 审计调用链，禁止锁嵌套 / 持 HID 锁调 ProcessEvents。

**验收**：`smoke-boot`；dual 下键鼠正常；`msc claim` 不 sick、不假死鼠；**仍可见** `fallback msc-claim`。建议连跑数分钟无死锁。

---

### PR-H-xhci-evt-excl-3：去掉 claim→poll（✅ TG `c26117c`）

**目标**：claim 保持 dual。

**主要文件**：`XhciMsc.c`（去掉 `XhciFallbackToPoll("msc-claim")` 及仅为该让步服务的分支）。

**验收**：

```bash
cd ../ToyImage && TOY_USB_MSC=1 ./smoke-msc.sh
# 或 Shell：msc claim — 日志无 fallback msc-claim；mode 保持 dual（或 QEMU 已升 irq）
```

- **NUC 必测**：插盘 claim + 打字/鼠标；无 `cmd sick`、无 irq-stall 假死。  
- 无 NUC **不并**本刀。翻车只回滚 3。

---

### （可选）PR-H-xhci-evt-excl-4（✅ TG `a01c351`）

**已做**：
- `RecoverCommandRing`：CA→排空→重建 **全程** `EnterExclusive`（含 WaitClearMs）。
- `Command` 二次超时标 sick **前**再 Recover + 重武装 HID（不留挂起命令 TRB）。
- PHOTO / `show xhci`：计数增加 **`x=`** = `gStatIrqSkipped`。

**回归**：`smoke-boot` + `TOY_USB_MSC=1 smoke-msc`；`show xhci` 可见 `x=`。

---

## 六、怎么测（每刀附）

```bash
cd ToyKernel && ./build.sh
cd ../ToyImage && ./smoke-boot.sh

# excl-1/2：claim 仍可出现 boot: xhci irq=poll (fallback) msc-claim
# excl-3：TOY_USB_MSC=1 ./smoke-msc.sh 或 msc claim — 不应再 fallback msc-claim
# excl-3 真机：NUC 插 U 盘 claim + 打字/鼠标
```

真机：NUC 可测 excl-3（及 1/2 回归键鼠）；不必为 e1000e。

---

## 七、与其它柱关系

- **H4e-2…** 暂缓，本柱优先。  
- **msc-6…8** 已 TG；本柱只改 claim 时 xHCI 模式让步，**不改**挂载语义。  
- 搬家约束见 [`大文件拆分.md`](大文件拆分.md) / [`XHCI拆分2.md`](XHCI拆分2.md)：本柱**不**搬 BSS。

---

## 八、速查表

| 序 | PR | 一句话 | 状态 |
| -- | --- | --- | --- |
| 1 | **excl-1** | 独占 API + 门铃同窗 + 全 Wait* + 守门；保留 claim→poll | ✅ TG `7c28c51` |
| 2 | **excl-2** | `gEvtConsumerLock` + ProcessEventsLocked | ✅ TG `e982dd6` |
| 3 | **excl-3** | 去掉 `fallback msc-claim`；真机永留 dual | ✅ TG `c26117c` |
| 4 | excl-4 | Recover/sick/PHOTO `x=`（可选） | ✅ TG `a01c351` |
