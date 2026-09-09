# 家 ↔ 公司：真机 xHCI 交接（给 Cursor / 人）

> **分支**：`home/xhci-pr1-5-bundle`  
> **计划全文**：[`real-pc-usb-pr-split.md`](real-pc-usb-pr-split.md)  
> **路线图家里轨**：[`路线图.md`](路线图.md) §家里轨  
> 家里开新 Agent 时：先 `@` 本文件 + `@Documents/real-pc-usb-pr-split.md`，再说 JX。

## 一句话

枚举与 poll arm 已通；**PHOTO 按键可涨 `k=`**（家侧已见 `k=20` / `i=20`）。  
当前刀 = **PR-H-xhci-base**：修桌面/Shell 假死（poll 空窗 + Shell 先 Halt）。

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
| `irq=poll` + `sync kbd deq` + `rearm` | poll 武装 OK |
| PHOTO `t/i/k` 随按键涨（如 `k=20`） | **中断 IN + 入队 OK** |
| `arms kbd=09/03 mouse=00/00`、`m=0` | 本机可无鼠标槽 |
| 无串口能打字；有串口又不能 + 电源长按 | Shell 曾 **无上限** 抽 COM1 RX；现每轮最多 32 字节再 Halt。CoolTerm→Shell **保留** |
| 枚举 `FAIL ControlXfer got=0x06` | xHCI **Stall**；多为 hub/非 HID 探测失败后 `DisableSlot`，键盘仍可 `input backend ready` |
| 曾见 `FAIL SetTrDeq got=0x13` + `kbd-intr cc=26` | 已改 Stop→排空→InitRing→SetTrDeq |

## 代码锚点

- `HAL/X64/Drivers/XHCI.c`：ResetPort、priv rings、`SyncIntrDequeue`、`ProcessEvents`、Drain、PHOTO
- `HAL/X64/Drivers/InputXhci.c`：Probe / Arm
- `HAL/X64/HalSerial.c`：BootMark、PhotoHold
- `Common/Core/KernelModules.c`：`usb` + PhotoHold；gui 内插 Poll
- `Common/Core/Module.c`：模块间 `HalInputPoll`（真机）
- `Common/Services/Tasks.c`：Shell **先 Poll/Dequeue 再 Halt**

## 协作

| 暗号 | 动作 |
|------|------|
| **JX** | 只改下一刀；smoke；**不** commit |
| **TG** | 验证后 commit + push 三仓相关部分 |

勿把公司机 `~/.cursor/plans/*.plan.md` 当唯一真相（不进 Git）。以本目录文档为准。

| `fs: mounted N volume(s), default=ESP` 且 `ls` 见 `EFI` | 只挂到了 ESP；或 Block 后端是机器 NVMe（无 USB MSC），看不到 U 盘 TOYOS 分区。看 `vols`：应有 `TOYOS` 才对 |

## 家侧 2026-09-09 晚

打字/电源 OK。三刀：
1. **Present 批处理**：`RunLine` 期间 `GuiPresentDefer*`，避免 help 真机逐行刷 GOP。
2. **多 FAT 挂载**：`GptFindAllFat` 同盘挂 ESP+TOYOS；有 `TOYOS.ID` 作默认。
3. **CoolTerm CR+LF**：串口吞 CR 后的 LF，避免双 `toyos>`。

若仍 `default=ESP`：串口有 `boot: nvme drives=` 而无第二盘时，运行时 Block 看不到 U 盘（尚无 USB MSC）——数据须在 NVMe 的 TOYOS 分区，或后续做 MSC。

## 建议下一刀（JX）

1. 冷启动：`help` 真机应接近一次刷出；`vols`/`ls` 默认 TOYOS（U 盘双区且 Block 能见该盘时）
2. CoolTerm `ls` 只一个 `toyos>`
3. smoke PASS
