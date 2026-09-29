# 声卡驱动 · Intel HDA（PR-G-audio · 活文档）

> **目的**：NUC / QEMU 能 **播一段 PCM**（蜂鸣 / `play` / 课堂演示），补齐「能看见也能听见」。  
> **排期指针**：路线图 ★ [`PR-G-audio-4`](../路线图.md#pr-g-audio-4)；柱总览活文档本文。  
> **权威代码**：`HAL/X64/Drivers/Hda/` +（后续）`HalAudio*`。  
> **日期**：2026-09-29 · **★ audio-4**；audio-0…3 ✅ TG（0/1 已 GD）。

---

## 0. 一句话结论

| 问 | 答 |
| -- | -- |
| 做什么？ | **播放**（输出）；先单流、固定格式（如 48kHz / 16bit / 立体声）。 |
| 录音 / 混音台 / 蓝牙音响？ | **不做**（本柱）。 |
| 靶机？ | **NUC7i7DN H** 板载 **Intel HD Audio**（Sunrise Point-LP 一类）；QEMU `intel-hda` + `hda-duplex` 做冒烟。 |
| 总工期？ | 初估 **约 10～18 日历日**（顺；CORB/Stream 卡死可到 3～4 周）。**乐观 5～8 日**（路径顺、少手测轮）。 |
| 谁干活？ | **Agent** 写码 + QEMU；**你** NUC 插耳机/音箱手测。 |

> 工期教训（igpu）：初估按「真机环/寄存器卡死」留余量，实耗可远小于上限。HDA 同属真机 MMIO 柱，**表内取中位排期，上限当风险缓冲**。

---

## 1. 现状与红线

**现在**：无音频驱动；`DeviceEnum` 仅把 PCI class `04xx` 显示为 `"audio"` 字符串；无 `HalAudio*`、无播音 syscall/App。

**红线**

- 驱动只许进 **HAL**；Common / Gui 只走 `HalAudio*`。
- **不**做完整 ALSA/Pulse；**不**多卡矩阵；**不** HDMI 音频（可后挂，本柱默认模拟线/耳机）。
- 任一步失败 → **软退**，不得挡 `ToyOS ready`。
- 新 `.c` ≤300 行；三架构可编（非 x86 **空桩**）。
- 串口 **禁止**打印采样数据洪流；黄字只指纹 / 状态。

---

## 2. 靶机与 QEMU

| 项 | 值 |
| -- | -- |
| 机型 | Intel **NUC7i7DN H**（机身无 3.5mm；听音 = **HDMI → 显示器耳机孔**） |
| 预期 PCI | Vendor `8086` + class `0403`；NUC7 **DID=`0x9D71`** @`00:1F.3` |
| MMIO | BAR0=`0xDF240000` sz=`0x4000`；`gcap=0x9701` `v=1.0` `outpay=0x3C` `inpay=0x1D` |
| Codec | **Intel HDMI** `8086:280B`；须 `EN_ALL_PIN_CVTS`+`DP12`；pin **5/6/7**=PORT B/C/D；本课活跃 **pin=6 cvt=2**（DP，DELL U2415） |
| 听音 | 显示器耳机孔；Ubuntu=`DP-2`→pin6；无 sense 时强制 pin6/cvt2；显示侧 `AUD_PIN_ELD_CP_VLD` |
| QEMU | `-device intel-hda -device hda-duplex`（或项目现有 run 脚本等价项）；无设备 → 软退 |

---

## 3. 分工与工期单位

| 角色 | 做啥 |
| ---- | ---- |
| Agent | 实现、`./build.sh`、QEMU 冒烟（能出波形/日志）；改文档 |
| 你 | NUC HDMI 接显示器，**耳机插显示器孔**手测；无声时看串口 / 断电 |

| 单位 | 含义 |
| ---- | ---- |
| **日历日** | Agent 当日能合完一刀并过 QEMU |
| **手测轮** | 你当晚 NUC 验一次；失败则次日修 |

---

## 4. 工期总表（拆 PR）

| 刀 | 一句话 | Agent 码 | NUC 手测 | 日历（顺） | 风险 |
| -- | ------ | -------- | -------- | ---------- | ---- |
| [audio-0](#5-pr-g-audio-0--pci-认卡) | PCI 认卡 + `lsdev` | 0.5 日 | 1 轮 | **1 日** | 低 |
| [audio-1](#6-pr-g-audio-1--mmio--控制器指纹) | BAR 映入 + 版本/全局帽只读 | 1 日 | 1 轮 | **1～2 日** | 低 |
| [audio-2](#7-pr-g-audio-2--corb--rirb--codec-枚举) | CORB/RIRB + 枚举 codec/widget | 2～3 日 | 2～3 轮 | **4～8 日** | **高** |
| [audio-3](#8-pr-g-audio-3--输出流--dma) | 输出 Stream + BDL + 单缓冲 DMA | 2～3 日 | 2～3 轮 | **4～8 日** | **高** |
| [audio-4](#9-pr-g-audio-4--halaudio--播-pcm) | `HalAudio*` + 内核播 WAV/PCM | 1～2 日 | 1～2 轮 | **2～4 日** | 中 |
| [audio-5](#10-pr-g-audio-5--shellplay--验收) | `play`/小 App + NUC 验收 | 1 日 | 2 轮 | **2～3 日** | 中 |
| **合计** | 能听见蜂鸣/短曲 | **~8～12 人日** | **~9～12 轮** | **~10～18 日** | — |

**关键路径**：0→1 可连做；**2→3 真机连续**（抢 NUC，勿与其它真机 MMIO 柱并行）。

```text
周1      audio-0 + audio-1     认卡稳
周2～3   audio-2               CORB/codec（可能反复）
周3～4   audio-3               Stream/DMA（可能反复）
周4～5   audio-4 + audio-5     API + 听见声音
```

---

## 5. PR-G-audio-0 · PCI 认卡

> **状态**：**✅ GD**（原 TG；2026-09-29；NUC `did=0x9D71 @00:1F.3` + bound）。  
> **一句话**：扫到 Intel HDA（或 QEMU intel-hda）→ 串口 DID + `lsdev=hda`；**不**映 BAR。

| 项 | 内容 |
| -- | ---- |
| 改 | `HAL/X64/Drivers/Hda/Hda.c`；`TOY_DRIVER_CLASS_AUDIO`；`HalDevices` 注册并 Probe |
| 不改 | 任何 MMIO 写；Gui |
| 验收 | NUC probe+bound（✅ `0x9D71`）；QEMU skip（✅）；`smoke-boot` 绿（✅） |
| 下一刀 | audio-1 |

---

## 6. PR-G-audio-1 · MMIO / 控制器指纹

> **状态**：**✅ GD**（原 TG；2026-09-29；NUC `bar=0xDF240000 gcap=0x9701 v=1.0 outpay=0x3C`；屏不黑）。  
> **一句话**：VMM 后 UC 映 BAR0；只读 GCAP / VMAJ / OUTPAY 等指纹黄字。

| 项 | 内容 |
| -- | ---- |
| 改 | `HdaMmio.c`；`HalHdaMmioInit`（Video 模块末，同 igpu-1） |
| 不改 | CORB 提交；DMA；写 GCTL |
| 验收 | NUC mmio 指纹（✅）；屏不黑（✅）；smoke 绿（✅） |
| 下一刀 | audio-2 |

---

## 7. PR-G-audio-2 · CORB / RIRB / codec 枚举

> **状态**：**✅ TG**（2026-09-29；NUC `corb entries=0x100`；`codec a=2 vend=8086280B` pin=`3` cfg=`0x18560010`）。  
> **一句话**：建 CORB/RIRB；发 GET 类 verb；列出 codec addr + 输出 pin/DAC widget（打表，不追求全图）。

| 项 | 内容 |
| -- | ---- |
| 改 | `HdaCorb.c` / `HdaCodec.c`；DMA 缓冲身份映；`HalHdaCodecInit` |
| 不改 | 播放流；IRQ（poll） |
| 验收 | NUC corb+HDMI pin（✅）；屏不黑；QEMU skip（✅） |
| 下一刀 | audio-3 |

---

## 8. PR-G-audio-3 · 输出流 + DMA

> **状态**：**✅ TG**（2026-09-29；NUC 显示器耳机孔有蜂鸣；`aud eld=0x444` pin6/cvt2）。  
> **一句话**：配置输出 Stream；BDL 环；PCM→HDMI/DP converter；DIP InfoFrame + 显示侧 AUD；poll 完成。

| 项 | 内容 |
| -- | ---- |
| 改 | `HdaStream.c` / `HdaHdmi.c`；48k/16/2；`HalHdaStreamInit`；`AUD_PIN_ELD_CP_VLD` |
| 不改 | 多流混音；HalAudio API（→audio-4） |
| 验收 | NUC `stream ok` + **耳机孔听见**（✅）；smoke 绿（✅） |
| 下一刀 | audio-4 |

---

## 9. PR-G-audio-4 · HalAudio + 播 PCM

> **状态**：**★ JX 待做**（2026-09-29）。  
> **一句话**：`HalAudioProbe/PlayPcm/Stop`；内核侧播内置短蜂鸣或 RootFs `BEEP.WAV`（小文件）。

| 项 | 内容 |
| -- | ---- |
| 改 | `Include/HalDevices.h` / `HalAudio*`；Arm/RiscV 空桩；复用 HdaStream |
| 不改 | 用户态完整 mixer；阻塞策略可先「播完返回」 |
| 验收 | 可触发一短声；软退静音；smoke 绿 |
| 下一刀 | audio-5 |

---

## 10. PR-G-audio-5 · shell/play + 验收

> **一句话**：Shell `play <file>` 或小 `PLAY.ELF`；文档验收清单勾完。

| 项 | 内容 |
| -- | ---- |
| 改 | Shell 命令或 Apps；可选极简 syscall（若必须用户态播） |
| 不改 | 商店/流媒体 |
| 验收 | NUC 插听：蜂鸣 + 短 WAV；QEMU smoke 不回归；柱收官可 TG |
| 下一刀 | 柱收官（★ 另选题） |

---

## 11. 验收总清单（柱完）

- [ ] NUC/QEMU 串口有 hda 认卡或明确软退  
- [ ] codec 枚举看得到输出路径  
- [ ] 耳机/音箱能听见短 PCM  
- [ ] `smoke-boot` 绿；无卡机器行为与今日一致  
- [ ] 失败软退：桌面/就绪不挡  

---

## 12. 明确不做（本柱）

| 不做 | 原因 |
| ---- | ---- |
| 录音 / 回声消除 | 课演示「有声」即可 |
| 软件混音多 App 抢声卡 | 单流；多源以后另柱 |
| DisplayPort 专用 infoframe | 本靶 HDMI；DP 另测 |
| USB Audio Class | 另一套驱动 |
| Windows/Linux 兼容声栈 | 教学 HAL，不是 ALSA |

---

## 13. 相关文档

| 文档 | 用途 |
| ---- | ---- |
| [`../路线图.md`](../路线图.md) | ★ / 候补 |
| [`../技术手册.md`](../技术手册.md) | HAL 边界 |
| [`Intel核显2D-blit.md`](Intel核显2D-blit.md) | 真机 MMIO 柱模板（工期写法） |
| [`开机流程与加速.md`](开机流程与加速.md) | 模块挂载点参考 |
