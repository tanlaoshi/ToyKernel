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
| 键鼠均可；`m` 随动鼠涨 | **2026-09-10**：hub 子口鼠 + 根口鼠均已通 |
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

**QEMU 鼠标**：曾对 `usb-tablet`(Proto=0) 误发 `SET_PROTOCOL` → Stall → 无 `xhci-hid mouse`。现仅 Proto=1/2 发 SET_PROTOCOL；tablet 走 `mouse (abs)`。

**真机鼠标**：旧逻辑只扫「其它根口」，复合键鼠（同 slot 第二 HID）永远 `mouse=00/00`。现 `InitMouseOnKeyboardSlot` 追加同设备鼠标 EP；期望 `boot: xhci-hid mouse (composite)` 且 `arms … mouse=NN/MM`。

家侧日志已见 `mouse (composite)` + `arms kbd=08/03 mouse=08/05`，但 PHOTO **`m` 不涨**：键盘 EP Running 时只 Add 鼠标 EP，部分 HC 上鼠标中断假配置。已改：Stop 键盘 → Drop+Add 键鼠一次配齐 → 两端点重新 Queue；并打 `mouse iface/proto/ep/mps`。

PHOTO 串口证据（2026-09-09）：`proto=0 mps=4`，`k` 涨、`m=0`、`u=0`、`s=08.03`（仅键盘完成）。根因候选：① TRB 长度曾固定 8>MPS=4 → 已改为 ≤MPS；② port3 `no hid ep` 可能是 **iface class 9 hub**（device class=0）→ 已认 hub iface；③ proto=0 可能是附加 HID 非指针 → 解析优先 boot mouse(3/1/2)。

## 家侧 2026-09-10（TG）

- **hub 鼠 `m=0`**：键/hub/鼠共用 EP0 环，Address 子设备 `InitRing` 踩坏 hub EP0 → TT 中断哑火。已拆分 EP0 环；hub 用 ConfigEP+TTT（对齐 EDK2）。
- **光标卡**：真机 `hlt` 等 tick + save-under ReadPixel。已 busy-poll（偶发 hlt）+ XOR 光标 + 合并 Present/拖动。
- 期望日志：`ep0=split` / `hub ttt=` / `xhci-hid mouse`（或 `via hub`）；PHOTO `m` 涨；桌面跟手。

## 消费级机对照（与 NUC/工控分开；暂不动）

| 机型 | PHOTO 要点 | 输入结果 |
|------|------------|----------|
| **NUC / 工控** | hub/分口键鼠、`kbd-v8` | 键鼠可用 |
| **ASUS N56VZ** | 8 口 `PORTSC=0x2A0`、**CCS=0** | PS/2 键 OK；USB 鼠废 |
| **台式机**（2026-09-11） | 反复 **`addr cc=0x04`**；`ResetPort` **not PED** / **reset timeout**；PS/2 self-test fail | **`input NONE`**；屏 `3840x2560` |

笔电偏「根口看不见设备」；台式偏「有尝试但 Address/Reset 失败」。路线图 §1.0·C 已并成一条「消费级机 xHCI」。**勿为这两台改动 NUC 已通路径，除非单独开刀并回归 NUC。**

## 建议下一刀（JX）

1. 路线图当前刀：**PR-H-xhci-dual**
2. **PR-G-fb-wc** ✅ TG：PHOTO/`fb-pte` 应见 `cache=WC`；另有 `boot: fb-wc ok`
3. 消费级机 / 真机分辨率 / 双键盘 → 边角暂缓
4. smoke PASS；真机相关改动必回归 NUC/工控