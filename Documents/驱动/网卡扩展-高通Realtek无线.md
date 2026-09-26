# 网卡扩展规划：高通 Atheros / Realtek / 无线

> **状态**：规划稿（有线 alx/rtl ✅；**无线选型 ✅ TG** → [`§3`](#3-无线选型--已钉pr-n-wifi-0--2026-09-26)）。  
> **栈契约**：只实现 [`NIC_L2`](../Include/DriverNic.h) → `NetAttachNic`；勿碰 Common 协议 / lwIP。  
> **活范例**：[`驱动开发范例-网卡L2.md`](驱动开发范例-网卡L2.md)（e1000 路径）。  
> **排期正文**：[`路线图.md`](../路线图.md) [`wifi-0`](../路线图.md#pr-n-wifi-0) ✅ → ★ [`wifi-1`](../路线图.md#pr-n-wifi-1) → wifi-2。

---

## 0. 一句话

N56VZ 板载 **Qualcomm Atheros AR8161**（PCI `1969:1091`）与常见 **Realtek RTL81xx** 走与 e1000 相同的 L2 门；无线另开柱，先选型再 MVP，不做「全芯片族产品化」。

```text
已有                              本柱要补
─────────────────────            ────────────────────────────
NIC_L2 + NetAttachNic            alx（AR8161）
e1000 / e1000e / I219 ✅         r8169（RTL8168/8111…）
virtio-net（课堂）               wifi（下一柱：一颗芯片 MVP）
```

---

## 1. 硬件与范围

| 目标 | PCI / 形态 | 机型 / 场景 | 本柱目标 |
| ---- | ---------- | ----------- | -------- |
| **alx** | VID `1969`，DID `1091`（AR8161）等 | N56VZ 板载有线 | `ping` 局域网网关；`lsdev` 见 `alx` |
| **r8169** | VID `10EC`，RTL8168/8111/8169 常见 DID | 消费级笔电/台式；QEMU 可辅 `rtl8139` 课堂 | 同上 |
| **wifi** | **已钉**：USB **RTL8188EU/EUS**（见 §3） | **NUC 与 N56VZ 双机**同棒必通 | 扫 AP + WPA2-PSK（或 open）+ DHCP |

**明确不做（整柱通用）**

- 搬 Linux 整棵 `alx` / `r8169` / `mac80211` 进树。  
- 硬件 offload / TSO / 多队列 / SR-IOV。  
- 全 Realtek / 全 Atheros / 全 ath10k/11k 产品化矩阵。  
- 无线：Wi‑Fi 6E/7、企业 EAP、软 AP、漫游。

**与旧「不做」条款**：路线图原「无线网卡产品化 / Realtek」改为 **本柱可排期**；产品化矩阵仍不做。

---

## 2. PR 拆分（有线 → 无线）

> 原则：每刀 ≤ 课堂可讲完；新 `.c` ≤300；无卡 Probe 失败不挡桌面；先 `lsdev` 再见包。

```mermaid
flowchart LR
  mux["✅ PR-H-input-mux 待 TG"]
  alx1["★ PR-N-alx-1 Probe/MAC"]
  alx2["PR-N-alx-2 TX/RX+L2"]
  rtl1["PR-N-rtl-1 Probe/MAC"]
  rtl2["PR-N-rtl-2 TX/RX+L2"]
  wifi0["PR-N-wifi-0 选型"]
  wifi1["PR-N-wifi-1 驱动骨架"]
  wifi2["PR-N-wifi-2 关联+DHCP"]
  mux --> alx1 --> alx2 --> rtl1 --> rtl2 --> wifi0 --> wifi1 --> wifi2
```

### 柱 A · 有线

| 序 | PR | 交付 | 验收 | 非目标 |
| -- | -- | ---- | ---- | ------ |
| 1 | **PR-N-alx-1** | `HAL/X64/Drivers/Alx/`：PCI 认 `1969:1091`（可加同族 1～2 个 DID）；BAR MMIO；读 MAC；`TOY_DRIVER` 名 `alx`；**不** `NetAttachNic` | 三架构可编；N56VZ `lsdev` 有 `alx`；串口 `Boot: alx MAC=…`；无卡机不挡桌面 | TX/RX；QEMU（无此卡属正常） |
| 2 | **PR-N-alx-2** | TX/RX 环或 FIFO；`NIC_L2`；`NetAttachNic`；`GetLink`；注册进 `HalDriverRegister` | N56VZ：`ping <网关>` 通；`smoke-boot` 默认 virtio 不回归挂 | Jumbo；MSI 必开（可先轮询） |
| 3 | **PR-N-rtl-1** | `HAL/X64/Drivers/Rtl/`：认 `10EC` 常见 GbE DID（8168/8111…）；MAC；`lsdev`=`r8169` | 有卡机 `lsdev`；无卡不挡；可选 QEMU `-nic …,model=rtl8139` **仅当**本刀含 FE 兼容或另记「8139 另刀」 | 把 8139 与 8169 混成一团糊 |
| 4 | **PR-N-rtl-2** | TX/RX + `NetAttachNic`；真机或 QEMU 通 `ping` | `smoke-boot` PASS；至少一条真机或 QEMU ping 路径写入本 PR 验收表 | 全 DID 表；固件 blob 大文件 |

**建议实现顺序细节（alx）**

1. 对照 Linux `drivers/net/ethernet/atheros/alx/` **只读**寄存器图与复位序列，手写最小子集。  
2. 复位 → 读 MAC → 开链路轮询 → 再开环。  
3. RX 路径统一 `NetInputFrame`（与 e1000 同）。

**建议实现顺序细节（rtl）**

1. 优先 **RTL8168/8111**（笔电常见）；寄存器以 Realtek GbE 手册 / `r8169` 为参考。  
2. QEMU：`rtl8139` 与 `r8169` **不是同一驱动**——课堂若必须 QEMU，可加薄 `rtl8139` 子文件或 `-device e1000` 继续当 L2 回归；**真机 Realtek 以 r8169 为准**。

### 柱 B · 无线（有线 TG 后再 ★）

| 序 | PR | 交付 | 验收 | 非目标 |
| -- | -- | ---- | ---- | ------ |
| 5 | **PR-N-wifi-0** | 本文 §3 **已钉**：USB **RTL8188EU/EUS**；**NUC+N56 双机硬验收**；风险表 | 文档合入；唯一主路径；双机契约写清 | 写驱动代码 |
| 6 | **PR-N-wifi-1** | 选定芯片：固件加载 / 探针 / `lsdev`；尚无数据面可先 `HalNetReady=0` | 真机见驱动绑定日志；无卡不挡 | 关联；扫 AP UI |
| 7 | **PR-N-wifi-2** | 扫 AP（Shell 即可）；open **或** WPA2-PSK 择一；DHCP + `ping` | 课网可演示「连上 → ping」 | WPA3；漫游；软 AP |

---

## 3. 无线选型 · **已钉**（PR-N-wifi-0 · 2026-09-26）

> **本刀只定契约**：一颗芯片 + 总线 + 课机；**不写驱动**。后续 [`wifi-1`](../路线图.md#pr-n-wifi-1) / [`wifi-2`](../路线图.md#pr-n-wifi-2) 只服务本表主路径。

### 3.1 唯一主路径

| 项 | 已钉 |
| -- | ---- |
| **形态** | **USB 无线网卡棒**（可插拔课堂演示；不绑死某台笔电板载） |
| **芯片族** | **Realtek RTL8188EU / RTL8188EUS**（USB 2.0 · 802.11n · 单频 2.4G） |
| **USB ID（认领用）** | VID **`0BDA`**（Realtek）；常见 PID 以棒为准（例：`8179` / `0179` 等）——wifi-1 Probe 用「`0BDA` + 已知 8188EU PID 白名单」，未知 PID 不挡桌面 |
| **总线** | **USB**（N56VZ → **EHCI**；NUC → **xHCI**；均已有 MSC/HID/UART 通路径） |
| **课机（硬）** | **NUC 与 N56VZ 都必须通**（同一根 8188EU 棒；EHCI/xHCI 各验）；演示不依赖任一方板载 Wi‑Fi |
| **关联** | wifi-2：**WPA2-PSK**（课网密码）为主；**open** 作无密码实验网备选 |
| **上栈** | 仍只出以太网帧 → `NetInputFrame` / 现有 lwIP（与有线 L2 同门）；**不**引入 mac80211 |

```text
同一根课堂棒 (RTL8188EU USB)
        ├─→ N56VZ · EHCI  ✅ 必通
        └─→ NUC    · xHCI ✅ 必通
                │
        HAL/X64/Drivers/Wifi/   ← wifi-1/2（双主机）
                │
        NetAttachNic(NIC_L2) 或等价薄适配
                │
        NetInputFrame / DHCP / ping
```

### 3.2 明确不选（本柱）

| 候选 | 理由 |
| ---- | ---- |
| ath10k / mt76 / Wi‑Fi 6 板载 | 固件 + 协议栈过重；违背「单卡 MVP」 |
| NUC 板载 Intel Wireless（如 `8086:24FD`） | 已见 lspci；无线产品化不做；也不作本柱主路径 |
| N56VZ 板载 Wi‑Fi（若为 ath9k 等 PCIe） | **加分项 / 另议**；需 `lspci` 确认后才值得并行；**不挡** USB 棒主路径 |
| USB Realtek 8812/8821 双频大棒 | 可后加 DID；首刀只冻 **8188EU** 一类，避免固件分叉 |
| QEMU 虚拟 Wi‑Fi | 无稳定课堂模型；无线验收以**真棒**为准 |

### 3.3 风险表（验收必读）

| 风险 | 影响 | 缓解 |
| ---- | ---- | ---- |
| **固件 blob** | 8188EU 常需加载 fw；许可与体积 | wifi-1：最小 fw 进 `Assets/` 或 rootfs 固定路径；文档写清许可来源；无 fw 则 Probe 失败不挡桌面 |
| **USB 主机差异** | N56=EHCI、NUC=xHCI | 棒走 USB2；两机 USB 外设已通；**wifi-1/2 双机都过才算柱通**（只通一台不算） |
| **同 hub 与 MSC/HID** | 抢带宽 / 枚举序 | 演示时棒单独口或与鼠同口时先插棒；不与大 U 盘抢同一演示节奏 |
| **WPA2 复杂度** | 主机端 crypto | wifi-2 可先 open 打通数据面，再加 PSK；**不做 WPA3 / EAP** |
| **PID 杂** | 山寨棒 VID/PID 乱 | 白名单 + 课堂指定采购型号；未知 ID 软失败 |
| **无 mac80211** | 不能「Linux 驱动直接搬」 | 手写最小 STA：扫 AP / 关联 / 数据；对照 Linux **只读** |

### 3.4 采购 / 识别（给人）

1. 买标称 **RTL8188EU** / **8188EUS** 的 USB Wi‑Fi 棒（免驱宣传可忽略，Guest 自有驱动）。  
2. 宿主 `lsusb` 见 `0bda:…`；记下 PID 写入 wifi-1 白名单。  
3. 课机：**先 NUC、再 N56**（或反过来）各插同一根棒 → wifi-1 两机都见黄字 / `lsdev` 约定驱动名（名在 wifi-1 钉，建议 `rtl8xxxu` 或 `rtl8188eu`）；**只通一台不算验收**。

### 3.5 候选对照（历史备忘 · 已否决为主路径）

| 候选 | 总线 | 优点 | 风险 | 结论 |
| ---- | ---- | ---- | ---- | ---- |
| **USB Realtek 8188EU** | USB | 易插拔；两端 USB 栈已通 | fw 许可；PID 杂 | **✅ 已钉主路径** |
| ath9k 一代 PCIe | PCIe | 文档多；无复杂 fw | 绑死板载；N56 是否 ath9k 未确认 | 加分 / 另议 |
| ath10k / mt76 | PCIe | 较新 | fw + 栈过重 | ❌ 不选 |
| Intel 板载（NUC） | PCIe | 机内已有 | 产品化；驱动重 | ❌ 不选 |

---

## 4. 目录与命名约定

| 路径 | 说明 |
| ---- | ---- |
| `HAL/X64/Drivers/Alx/` | `Alx.c` / `AlxProbe.c` / `AlxHw.c` / `AlxPrivate.h`… 每文件 ≤300 |
| `HAL/X64/Drivers/Rtl/` | 同上；对外驱动名 `r8169`（即使用户口语 Realtek） |
| `HAL/X64/Drivers/Net/NetAlx.c` | 可选薄封：`NIC_L2` + Register（对齐 `NetE1000.c`） |
| `HAL/X64/Drivers/Net/NetRtl.c` | 同上 |
| `HAL/X64/Drivers/Wifi/` | wifi-1 起；**禁止**塞进 `Alx/` |

注册：仅 `HalDriverRegister` 增一行；Probe 失败静默。

---

## 5. 与 USB / 输入（现状）

- N56VZ：**EHCI** 鼠/FT232 + PS/2 键/触控板已通；板载有线 **alx `ping` 通**。  
- NUC：**xHCI** 键鼠 + I219 有线已通。  
- 无线主路径用 **USB 棒**，故意复用上述 USB 栈，避免再开 PCIe 无线深水。

---

## 6. 验收口令（给人）

| 机 | 命令 / 现象 |
| -- | ----------- |
| N56VZ | 有线：`lsdev`→`alx` / `ping`；无线：8188EU → wifi-1/2 **必通** |
| NUC | 有线：`lsdev`→`i219`；无线：**同一根** 8188EU → wifi-1/2 **必通** |
| Realtek 有线机 | `lsdev` → `r8169`（有卡手测后续） |
| 回归 | `./Scripts/smoke-boot.sh`；默认仍 virtio |
| 无线柱收口 | **NUC 与 N56 两机都** 扫 AP → WPA2-PSK（或 open）→ `ping`；只通一台不算 |

---

## 7. 参考（只读，勿整文件拷贝）

- Linux `drivers/net/ethernet/atheros/alx/`  
- Linux `drivers/net/ethernet/realtek/r8169*.c`  
- Linux `drivers/net/wireless/realtek/rtl8xxxu/` / `rtl8188eu`（**只读**序列与 fw 名，不搬 mac80211）  
- ToyOS `HAL/X64/Drivers/E1000/` + `Net/NetE1000.c`  
- [`已完/I219真机网课路径.md`](../已完/I219真机网课路径.md)（课路径写法模板）
