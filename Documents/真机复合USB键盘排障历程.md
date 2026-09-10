# 真机复合 USB 键盘排障历程（NUC 参考）

> **日期**：2026-09-10（办公室 NUC 迭代）  
> **场景**：Intel NUC + 复合 HID（同一 USB 地址上键盘 iface + 鼠标 iface）  
> **验收**：PHOTO 底栏 `k=`（键盘推送）/ `m=`（鼠标推送）/ `s=slot.ep`（最近完成事件）  
> **相关**：[`home-xhci-handoff.md`](home-xhci-handoff.md)、[`real-pc-usb-pr-split.md`](real-pc-usb-pr-split.md)、代码 `HAL/X64/Drivers/XHCI.c`  
> **状态**：鼠标已顺；键盘仍在迭代（见文末「当前刀」）。本文记录**试过什么、现象、结论**，供后续少走弯路。

---

## 0. 一句话

QEMU 上「独立 usb-kbd + usb-tablet」和好的路径，**不能直接搬到真机复合键鼠**。  
NUC 上反复出现：**鼠标中断 IN 正常（`m` 涨、`s=05.03`），键盘中断 IN 永不完成（`k=0`）**。  
根因更像是 **同 slot 上第二次/不当的 Configure Endpoint 与键 EP 生命周期**，而不是「没枚举到键盘」。

---

## 1. PHOTO 计数怎么读

底栏形如：

`PHOTO 24s t=682 i=682 k=0 m=681 u=0 s=05.03 c=13 r=682 d=…`

| 字段 | 含义 |
|------|------|
| `t` | 任意 Transfer Event |
| `i` | 匹配到键或鼠中断 EP 的完成数 |
| `k` | 键盘报告入队（`gStatKbdPush`） |
| `m` | 鼠标报告入队 |
| `u` | 未匹配的 xfer 事件 |
| `s=SS.EE` | **最近一次**完成的 slot / **DCI**（不是 PHOTO 秒数） |
| `c` | 最近完成码（`13`=Short Packet，鼠正常） |
| `r` | 事件环取出次数 |
| `d` | Drain 次数 |

**典型坏相**：`t≈i≈m`，`k=0`，`s=05.03` → 全程只有**鼠标 DCI=3** 在完成；键盘多为 DCI=5，日志里曾见 `kbd iface=01 dci=05 mouse dci=03`。

刷机验收：日志里必须出现 **`boot: xhci build=kbd-v4`**（Init 入口就打）。  
若只有 `mouse with-kbd cfg ok`、没有 `build=kbd-v4` / `kbd-fix=v4` → **仍在跑旧内核**。

**分区与同步（易混）：**

| 谁 | 看见什么 |
|----|----------|
| UEFI Boot | 能扫 U 盘 ESP + TOYOS；靠 **`TOYOS.ID`** 选系统盘加载 `Kernel.elf` |
| 内核 `vols` / FileSystem | 只枚举 **Block 层已挂上的盘**（真机现为 **NVMe** 等）；**尚无 USB Mass Storage** |
| 因此 | 进系统后敲 `vols` **看不到 U 盘上的 TOYOS** 是预期现状，不是 sync 写错盘 |
| PHOTO `fs: default=ESP (no TOYOS.ID)` | 内核在已见 FAT 里没找到带 `TOYOS.ID` 的卷（常见：只见机内 NVMe ESP），**不等于** Boot 没从 U 盘加载内核 |

- **Kernel.elf 只写 U 盘 TOYOS 分区**（给 Boot 用），不要写进 ESP。  
- `./sync-usb.sh --kernel-only`：只更新 TOYOS；打印时间与 md5。  
- 若要 `vols` 里出现 TOYOS：需要后续做 **USB 大容量存储**（或把 rootfs/`TOYOS.ID` 放到内核能看到的 NVMe 分区上）。

**PHOTO 底栏怎么一眼定位：**

| 你看到的 | 含义 |
|----------|------|
| `k=0 m>0` + 后缀 `!kbdIN=0` | 键中断 IN 没完成；鼠正常 |
| `s=05.03` | 最近完成在 **鼠标 DCI=3**（不是“秒数”） |
| `c=13` | Short Packet，鼠正常 |
| 没有满屏 `want=`/`got=` | 默认 `diag: quiet` 只藏 **OK**；**FAIL 仍会打 want/got**。全量 OK 需 `XHCI_DIAG_VERBOSE=1` |

---

## 2. 设备形态（为何和 QEMU 差很大）

| | QEMU（课堂默认） | NUC 真机（本轮） |
|--|------------------|------------------|
| 键鼠 | 常两个独立设备、两个 slot | **复合设备**：一个 slot，iface0=鼠 Proto=2，iface1=键 Proto=1 |
| 端点 | 各管各的 | 鼠 `ep=0x81`→DCI=3；键常 `ep=0x82`→DCI=5 |
| 中断 | MSI 好用 | 真机默认 **poll Drain**（`irq=poll`） |
| PS/2 | 基本不用 | 笔记本内置键常是 PS/2；NUC 外设是 USB |

Linux：**同一套** `xhci_hcd` + `usbhid`，按 interface 各自提 Interrupt URB，真机/虚拟机不写两套策略。  
ToyOS 当前是最小栈；对烂/刁的复合设备会先用 **quirk**（见 v4），再慢慢靠拢 Linux 模型。

---

## 3. 方法编年（按尝试顺序）

### 3.1 已确认有效（鼠标 / 输入框架）

| 方法 | 做法 | 结果 |
|------|------|------|
| 停掉「键+鼠双 Sync」 | Arm / 进桌面前勿对两路都 `SyncIntrDequeue` | 曾修 PHOTO `r=t=0`；鼠可动 |
| 鼠 TRB 长度 ≤ MPS | `QueueMouseIntr` 按 `gMouseReportLen`/MPS enqueue | 避免真机 `mps=4` 时永不完成 |
| 相对鼠禁止绝对坐标启发式 | Proto=2 不用高字节猜绝对坐标 | PHOTO `m` 涨且桌面光标能跟 |
| 复合鼠解析评分 | 真机 `gMouseParseScore < 2` 跳过弱 HID | 少绑到媒体键假鼠标 |
| PHOTO 延长 | `HalSerialGopPhotoHold` 上限放到 120；调用方约 30s | 方便真机拍照 |
| PHOTO 必须 Mute + 直写 front | 避免 Present/后缓冲与 xHCI poll 打架 | 否则 `k` 假死 |
| Drain 释锁后再碰 EP0 | 禁止持 `gHidQueueLock` 调 `ControlXfer` | 避免桌面死锁 / 电源键假死 |

### 3.2 键盘：试过但未解决（或只证明了边界）

| # | 方法 | 现象 | 结论 |
|---|------|------|------|
| A | **Drop+Add** 同一次 ConfigEP 重建键+鼠 EP | 鼠 OK，`k=0` | 重建键环/上下文易弄死键 IN |
| B | **Add-only 鼠标**（不 Drop 键），先 **Stop 键** 再 Config | `add-only ok`，鼠 OK，`k=0` | Stop 后再 Sync 也救不回 |
| C | Add-only **且不 Stop 键**；枚举后/Arm **不再 Sync 键** | 仍 `add-only ok`，仍 `k=0` | **第二次 ConfigEP 本身**就会搞死键 IN（即使 Add 位不含键） |
| D | **首次 ConfigEP 一次 Add 键+鼠**（`with-kbd cfg ok`） | 仍只有 `s=*.03`，`k=0` | 问题不单是「第二次 Config」；**同 slot 上键 IN 从一开始就不完成** 也成立 |
| E | Arm 只 Queue、不 Sync | 鼠更稳；键无改善 | Sync 是鼠/PHOTO 的雷，但不是键 `k=0` 的唯一因 |
| F | EP0 **GET_REPORT** 轮询键盘 | v4 上大量 `ControlXfer got=0x06`（Stall），`k` 仍 0 | 本设备 GET_REPORT 基本不可用；v5 默认关闭 |
| G | v4：**Reset EP** 重建键环 | `ResetEP/SetTrDeq got=0x13` Context State | **Running 时不能直接 Reset**；须先 Stop |
| H | （对比）笔记本 **PS/2** 键盘 | 与 USB 复合无关 | 分清设备类型 |

### 3.3 当前刀（v6 对照）

真机：**不 Prep/Add 复合鼠标**，只 Config 键盘 IN。  
目的：分清「键本身就不吐」还是「加鼠后弄死键」。  
独立口鼠标仍可 `InitMouseOnPort`。  
戳：`boot: xhci build=kbd-v6` / `kbd-only (no composite mouse)`。

电源短按：走 `AcpiPowerButtonPressed`（PM1 PWRBTN）；与 USB 正交。PHOTO 尾若无 `ACPI power ready` 则电源路径未就绪。

---

## 4. 诊断决策树（下次真机 `k=0`）

```text
PHOTO k=0？
 ├─ 日志有无本版戳（kbd-fix=vN）？
 │    └─ 无 → 先确认 Kernel.elf 已 sync 到启动 ESP（勿测旧镜像）
 ├─ s= 是否总落在鼠标 DCI（如 05.03）？
 │    └─ 是 → 键中断 IN 未完成（不是入队解析写错这么简单）
 ├─ 临时跳过复合鼠标（仅 Config 键盘）？
 │    ├─ k>0 → 鼠 Add/二次 Config 是凶手；朝「Linux 式按 iface URB + 拷 Output Context」修
 │    └─ k=0 → 键 EP 描述符/协议/Interval/MPS/门铃；或换端口/换键鼠再测
 ├─ GET_REPORT
 │    ├─ stall/give up → 设备不支持该用法；别死磕 EP0
 │    └─ 成功但全 0 → 协议/Report ID/iface 号可能错
 └─ 若是笔记本内置键 → 查 PS/2，不要只盯 xHCI
```

---

## 5. 和 Linux 的对照（预期方向）

| Linux | ToyOS 现状 | 后续宜靠拢 |
|-------|------------|------------|
| 每 interface 独立提交 Interrupt URB | 单文件手写键/鼠环 | 按 iface 状态机，少「整 slot 重建」 |
| ConfigEP 时慎重保留已 Running 的 EP | Add-only / 联合 Config 仍见键死 | 从 **Output Context** 拷贝未改 EP；少 Stop/Reset 邻居 |
| quirk 表处理烂设备 | 真机特判 GET_REPORT / Reset | 保留 quirk，但标成例外 |
| 真机与 QEMU 同一驱动 | 真机 poll + 若干 `!HalCpuIsHypervisor()` 分支 | 尽量缩小分支，用描述符驱动行为 |

「全套 USB」量级：主机 xHCI + USB 核心（枚举/hub/热插拔）+ 各类驱动；Linux 相关代码数十万行量级。ToyOS 只做课堂最小集足够，但**复合 HID 是最小集里的第一只拦路虎**。

---

## 6. 关键代码 / 日志锚点

| 项 | 位置 |
|----|------|
| 配置键/鼠、Add-only、Recover、GET_REPORT | `ToyKernel/HAL/X64/Drivers/XHCI.c` |
| PHOTO 底栏格式化 | `XhciDiagFormat` → `HalInputDiagFormat` → `HalSerialGopPhotoHold` |
| 输入 Bind / Arm | `InputXhci.c`、`HalDevices.c` |
| 串口/PHOTO Mute | `ToySerialConfig.h`、`HalSerial.c` |
| 版本戳（刷机核对） | BootLog：`boot: xhci kbd-fix=v4` |
| 成功鼠路径日志 | `mouse add-only ok` / `with-kbd cfg ok` / `mouse cfg i=…` |

---

## 7. 维护约定

- 每找到一种**对 NUC 有效或明确无效**的手法，在 §3 加一行（方法 / 现象 / 结论），并更新「当前刀」版本戳。  
- 不要删失败方法——失败结论比成功补丁更保值。  
- 家↔公司交接仍以 [`home-xhci-handoff.md`](home-xhci-handoff.md) 为准；本文专记**复合键盘**坑。

---

## 8. 当前未闭环项（写文档当日）

- [ ] NUC：`build=kbd-v5` 后 `k>0` 且 `m` 仍顺  
- [x] v4 PHOTO：`ResetEP/SetTrDeq got=0x13`（对 Running EP 直接 Reset 非法）→ v5 改为 Stop→SetDeq，失败再 Drop+Add 仅键盘  
- [x] v4 满屏 `ControlXfer got=0x06`：多半是 GET_REPORT Stall + FAIL 全开；v5 限 2 条 Stall 日志并默认关 GET_REPORT  
- [ ] 若 Sync/ReAdd 仍 `k=0`：做「仅键盘、不 Add 鼠标」对照  
- [ ] 电源短按 / ACPI  
- [ ] 长期：接近 Linux per-iface 中断 IN；`vols` 见 U 盘需 USB MSC  

（完）
