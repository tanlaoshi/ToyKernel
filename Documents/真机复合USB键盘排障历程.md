# 真机 USB 键鼠排障历程（NUC 参考）

> **日期**：2026-09-10（办公室 NUC 迭代 → 里程碑收官）  
> **硬件真相**：有线键盘 + Logitech G102 **分插两个 USB 口**（无接收器）  
> **初判失误**：日志像「同 slot 复合」——实为驱动把 G102 **附加 HID** 误当成键盘，再把同设备 boot 鼠绑成 composite  
> **验收**：PHOTO `k>` 且 `m>`；桌面可打字、可移鼠标；戳 `boot: xhci build=kbd-v8`  
> **代码**：`HAL/X64/Drivers/XHCI.c` · 合入 `fef65a1`（键鼠）/ `0b07402`（ACPI 短按，**真机已确认可关机**）  
> **状态**：**✅ 收官**。下文保留试错轨迹。路线图：[`路线图.md`](路线图.md) 文末【归档】里程碑 · NUC。  
> **对照**：[`今日USB键盘逻辑对照-开盘vs当前.md`](今日USB键盘逻辑对照-开盘vs当前.md)、[`home-xhci-handoff.md`](home-xhci-handoff.md)

---

## 0. 一句话（收官结论）

1. **假复合**：弱键盘分（score&lt;3）+ 像样鼠标（score≥2）→ **不当键盘**（`skip mouse+extraHID as kbd`）。  
2. **原地认领鼠**：已 Address 的口 **勿 Disable + 再 Address**（真机常 `addr cc=0x04` / Reset 超时 → `m=0`）；走 `ClaimAddressedSlotAsMouse`，EP0 仍跟 Address 时的 `gEp0`。  
3. **真键盘**：boot-protocol 键盘 score=3，**kbd-only Config**；再扫其它口绑鼠（或已 claim）。  
4. **真机禁同 slot 复合加鼠**：会弄死键 IN；QEMU 仍可 composite。

早期误以为「同 slot 第二次 ConfigEP 弄死键」——部分现象成立，但 **根因是绑错了设备**：真键盘从未正确占用。

---

## 1. PHOTO 计数怎么读

底栏形如：

`PHOTO 24s t=682 i=682 k=51 m=120 u=0 s=06.05 c=1 r=682 d=…`

| 字段 | 含义 |
|------|------|
| `t` | 任意 Transfer Event |
| `i` | 匹配到键或鼠中断 EP 的完成数 |
| `k` | 键盘报告入队（`gStatKbdPush`） |
| `m` | 鼠标报告入队 |
| `u` | 未匹配的 xfer 事件 |
| `s=SS.EE` | **最近一次**完成的 slot / **DCI**（不是 PHOTO 秒数） |
| `c` | 最近完成码（`13`=Short Packet，鼠常见；`1`=Success） |
| `r` | 事件环取出次数 |
| `d` | Drain 次数 |

| 坏相（历史） | 含义 |
|--------------|------|
| `k=0 m>0`，`s=05.03`，`!kbdIN=0` | 全程像只有「鼠口」在吐；常是 **假键盘 slot** 或真键未绑 |
| `k>0 m=0` + `skip mouse+extraHID` + Address/Reset fail | v7：拒认 G102 为键后，鼠口 **Disable+重 Address 失败** |
| 无 `build=kbd-v8` | **仍在跑旧 Kernel**（先核对 TOYOS 上 md5） |

**好相（收官）**：`build=kbd-v8`；`claim after skip-kbd`（或独立口 `xhci-hid mouse`）；**键鼠不同 slot**；`k>0` 且 `m>0`。

### 分区与同步（易混）

| 谁 | 看见什么 |
|----|----------|
| UEFI Boot | 扫 U 盘 ESP + TOYOS；靠 **`TOYOS.ID`** 选系统盘加载 `Kernel.elf` |
| 内核 `vols` | 只见 **Block 已挂盘**（NUC 常为 NVMe）；**尚无 USB MSC** |
| PHOTO `fs: default=ESP (no TOYOS.ID)` | 内核 FAT 里没找到带 `TOYOS.ID` 的卷（常见：机内 NVMe ESP）≠ Boot 没从 U 盘加载 |

- **Kernel.elf 只写 TOYOS**，不要写 ESP。  
- `./sync-usb.sh --kernel-only`：只更新 TOYOS；对一下时间与 md5。

---

## 2. 设备形态：初判 vs 真相

| | QEMU（课堂） | NUC（初判日志） | NUC（真相） |
|--|--------------|-----------------|-------------|
| 键鼠 | 两设备两 slot | 像「一 slot 复合」 | **两口两设备**；G102 另有弱 HID |
| 驱动曾 | 各绑各的 | 弱 HID→假键盘 + 同设备鼠→composite | v7 拒假键；v8 claim 为鼠 |
| 中断 | MSI 可用 | **poll Drain** | 同左（`irq=poll`） |

Linux：`xhci_hcd` + `usbhid` 按 interface 提 URB。ToyOS 是最小栈；假键盘是 **quirk + 评分** 先挡住，再靠分口枚举。

---

## 3. 方法编年

### 3.1 一直有效（框架 / 鼠）

| 方法 | 结果 |
|------|------|
| 停「键+鼠双 Sync」；Arm 只 Queue | 修 PHOTO `r=t=0`；鼠可动 |
| 鼠 TRB 长度 ≤ MPS；Proto=2 禁用绝对启发式 | `m` 涨且光标跟手 |
| 弱鼠标 score&lt;2 跳过 | 少绑媒体键假鼠标 |
| PHOTO Mute + 直写 front；Drain 勿持锁碰 EP0 | 避免假死 / 死锁 |
| firmware-first 环；真机禁裸 MSI 打断 poll | 输入框架能活 |

### 3.2 键盘弯路（证明边界 · 勿再踩）

| # | 方法 | 现象 | 结论 |
|---|------|------|------|
| A | Drop+Add 重建键+鼠 | 鼠 OK，`k=0` | 重建键环易死 |
| B/C | Add-only 鼠（Stop / 不 Stop 键） | `add-only ok`，`k=0` | 同 slot 二次 Config 危险 |
| D | 首次联合 Config 键+鼠 | 仍 `k=0` | 真机复合加鼠不可取 |
| E | Arm 只 Queue | 鼠稳；键无改善 | Sync 不是唯一因 |
| F | GET_REPORT 轮询键 | Stall `cc=6` | 勿作主路径 |
| G | Running 时 Reset EP | `got=0x13` | 须先 Stop |
| H | 笔记本 PS/2 | — | 与 USB 分清 |

### 3.3 版本戳演进 → 收官

| 戳 | 意图 | 真机结果 |
|----|------|----------|
| v4 | Reset EP + GET_REPORT | `0x13` + Stall 刷屏 |
| v5 | Stop→SetDeq；限 FAIL 日志 | Sync「ok」仍 `k=0`（仍绑错设备） |
| v6 | 真机 kbd-only，不复合加鼠 | 对照：指向「独立口鼠」方向 |
| v7 | `RealPcRejectMouseExtraAsKeyboard` | **键盘通**；鼠口再 Address 常失败 → `m=0` |
| **v8** | reject 后 **ClaimAddressedSlotAsMouse** | **键鼠都通**（里程碑） |

关键日志（v8）：

- `boot: xhci build=kbd-v8`
- `boot: xhci skip mouse+extraHID as kbd` + `kbd-score=… mouse-score=…`
- `boot: xhci-hid mouse (claim after skip-kbd)` 或 `xhci mouse on other port`
- `boot: xhci-hid keyboard`

---

## 4. 诊断决策树（回归时用）

```text
刷机后有无 build=kbd-v8？
 └─ 无 → sync TOYOS/Kernel.elf（勿写 ESP）

PHOTO / 桌面
 ├─ k=0 m>0，且无 skip mouse+extraHID
 │    └─ 仍像假复合：查 ParseConfig 评分是否又把弱 HID 当键
 ├─ k>0 m=0，有 skip…，有 Address/Reset fail
 │    └─ claim 路径未跑到或失败：查 ClaimAddressedSlotAsMouse / ForcePR
 ├─ k>0 m>0，不同 slot
 │    └─ 预期好相
 └─ 电源短按
      └─ ✅ 2026-09-10 NUC 已确认关机（`0b07402`：X_GAS / SCI_EN / PWRBTN_EN / `_S5_`）
```

---

## 5. 和 Linux 的对照

| Linux | ToyOS（收官后） | 仍可做 |
|-------|-----------------|--------|
| 每 iface 独立 Interrupt URB | 分口两 slot；真机不复合加鼠 | dual/IRQ 可选 |
| 慎改已 Running EP | claim 鼠保留 Address 时 EP0 | 少 Stop 邻居 |
| quirk 表 | reject 弱键 + claim 鼠 | 保持例外标注 |
| MSC 块设备 | 无 → `vols` 不见 U 盘 TOYOS | H-msc 暂缓 |

---

## 6. 代码 / 日志锚点

| 项 | 位置 / 串 |
|----|-----------|
| 评分拒假键 | `RealPcRejectMouseExtraAsKeyboard` |
| 原地认领鼠 | `ClaimAddressedSlotAsMouse`；`gSlotEp0UsesKbdRing` |
| 真机 kbd-only | 枚举里 `!HalCpuIsHypervisor()` 分支；`kbd-fix=v8` |
| 独立口鼠 | `InitMouseOnPort`（claim 失败时的 ForcePR 回退） |
| PHOTO | `XhciDiagFormat` → `HalSerialGopPhotoHold` |
| 刷机戳 | `boot: xhci build=kbd-v8` |
| 合入 | ToyKernel `fef65a1`；排障对照文档见文首 |

---

## 7. 维护约定

- 新坑：在 §3 加一行（方法 / 现象 / 结论），并改版本戳说明。  
- **失败结论比成功补丁更保值**——§3.2 不要删。  
- 标题虽曾写「复合」，正文以 **假复合 + 分口** 为准；交接总览仍见 handoff。

---

## 8. 闭环清单（里程碑）

- [x] 分清硬件：分口键鼠，非真复合  
- [x] 拒认鼠标附加 HID 为键盘（v7）  
- [x] claim 已 Address slot 为鼠，避免重 Address（v8）  
- [x] 真机不走同 slot 复合加鼠  
- [x] PHOTO / 桌面：`k>0` 且 `m>0`  
- [x] 路线图 / 协作日志里程碑已记  
- [x] 电源短按：NUC 真机确认可关机（`0b07402`，2026-09-10）  
- [ ] 长期：USB MSC；xHCI dual（可选）

（完）
