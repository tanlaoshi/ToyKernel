# 家 ↔ 公司：真机 xHCI 交接（给 Cursor / 人）

> **分支**：`home/xhci-pr1-5-bundle`  
> **计划全文**：[`real-pc-usb-pr-split.md`](real-pc-usb-pr-split.md)  
> **路线图家里轨**：[`路线图.md`](路线图.md) §家里轨  
> 家里开新 Agent 时：先 `@` 本文件 + `@Documents/real-pc-usb-pr-split.md`，再说 JX。

## 一句话

枚举与 poll arm 已通到「键鼠都 ready + PHOTO」；**按键仍不涨 `k=`**。当前刀 = **PR-H-xhci-base**。

## 同步命令（家里）

```bash
cd ~/…/ToyKernel
git fetch origin
git checkout home/xhci-pr1-5-bundle
git pull --ff-only origin home/xhci-pr1-5-bundle

cd ~/…/ToyImage
git pull --ff-only origin master

cd ../ToyKernel && ./build.sh
cd ../ToyImage
# 需要时：udisksctl mount -b /dev/sdX1
./sync-usb.sh --kernel-only
```

公司侧最近推送示例：`ToyKernel` `0cb2355`；`ToyImage` `fc9bef8`（`sync-usb.sh`）。

## 真机已证实

| 现象 | 含义 |
|------|------|
| `xhci OK` EnableSlot / Address / ConfigEP | 枚举 OK |
| `xhci-hid keyboard` + `xhci-hid mouse` | HID 找到（日志顺序曾乱，已改为 Init 内 keyboard→mouse→init returned） |
| `irq=poll (base)` + `arms kbd=06/05 mouse=08/05` | poll 武装 |
| PHOTO `k=0 m=0` 按键不变 | **中断 IN 报告未进队** |
| 曾见 `FAIL SetTrDeq got=0x13` + `kbd-intr cc=26` | Stop/SetTrDeq 顺序错；已改 Stop→排空→InitRing→SetTrDeq，Stopped 不重投门铃 |
| 读秒白字消失 | BootMark/ClearBody；已加 `gPhotoHold` |

## 代码锚点

- `HAL/X64/Drivers/XHCI.c`：ResetPort、priv rings、`SyncIntrDequeue`、`ProcessEvents` 匹配、`XhciDrainEvents`、PHOTO 统计
- `HAL/X64/Drivers/InputXhci.c`：Probe / Arm
- `HAL/X64/HalSerial.c`：BootMark、PhotoHold
- `Common/Core/KernelModules.c`：`usb` 模块 + PhotoHold(20)

## 协作

| 暗号 | 动作 |
|------|------|
| **JX** | 只改下一刀；smoke；**不** commit |
| **TG** | 验证后 commit + push 三仓相关部分 |

勿把公司机 `~/.cursor/plans/*.plan.md` 当唯一真相（不进 Git）。以本目录文档为准。

## 建议下一刀（JX）

1. 冷启动确认：有 `boot: xhci sync kbd deq`，**无** `FAIL SetTrDeq … 0x13`
2. PHOTO 按键看 `i=`/`k=`；若仍有 `kbd-intr cc=` 记下数值
3. 若 SetTrDeq 仍失败：考虑跳过 Sync、仅门铃/或 ConfigEP 重配中断 EP
4. smoke 必须 PASS
