# 网卡扩展规划：高通 Atheros / Realtek / 无线

> **状态**：规划稿（有线 alx/rtl ✅；**无线已改钉 iwl** → [`§3`](#3-无线选型--已钉pr-n-wifi-0-改钉-iwl--2026-09-26)）。  
> **栈契约**：只实现 [`NIC_L2`](../Include/DriverNic.h) → `NetAttachNic`；勿碰 Common 协议 / lwIP。  
> **活范例**：[`驱动开发范例-网卡L2.md`](驱动开发范例-网卡L2.md)（e1000 路径）。  
> **排期正文**：[`路线图.md`](../路线图.md) ★ [`wifi-1`](../路线图.md#pr-n-wifi-1)（NUC `Iwl` · `8086:24fd`）。

---

## 0. 一句话

N56VZ 板载 **Qualcomm Atheros AR8161**（PCI `1969:1091`）与常见 **Realtek RTL81xx** 走与 e1000 相同的 L2 门；无线主路径改钉为 **NUC 板载 Intel 8265/8275**（`8086:24fd`），不做「全芯片族产品化」。

```text
已有                              本柱要补
I219 / alx / r8169（有线）   →   NUC iwl 8265 无线 MVP
USB MSC / HID / UART         →   （无线走 PCIe，不依赖棒）
```

---

## 1. 范围

| 柱 | 芯片 / 总线 | 课机 | 验收 |
| -- | ----------- | ---- | ---- |
| **alx** | VID `1969`，DID `1091`（AR8161）等 | N56VZ 板载有线 | `ping` 局域网网关；`lsdev` 见 `alx` |
| **rtl** | Realtek PCI `10EC:8168…` | 有卡机 | 有卡手测；无卡软失败 |
| **wifi** | **已钉**：NUC **8265/8275** `8086:24fd`（见 §3） | **NUC 板载必通** | 扫 AP + WPA2-PSK（或 open）+ DHCP |

**不做**：
- 全芯片族矩阵、企业 EAP、软 AP、漫游、WPA3。
- 以 N56 ath9k / USB 8188EU 抢本柱 ★（随后柱 / 无货）。

**与旧「不做」条款**：无线 MVP **可排期**；产品化矩阵仍不做。

---

## 2. PR 拆分（有线 → 无线）

（有线 alx/rtl 表从略 · 见路线图 §七。）

### 柱 B · 无线

| # | PR | 内容 | 验收 | 非目标 |
| - | -- | ---- | ---- | ------ |
| 5 | **PR-N-wifi-0** | §3 **已改钉** iwl `8086:24fd`；fw 路径；N56 随后 | 文档合入；唯一主路径 | 写驱动 |
| 6 | **PR-N-wifi-1** | `HAL/X64/Drivers/Iwl/` Probe + 读 `FW/IWL8265.UCODE` + `lsdev` | NUC 黄字/`lsdev`；无卡/无 fw 不挡 | 关联 |
| 7 | **PR-N-wifi-2** | 扫 AP + WPA2-PSK/open + DHCP + `ping` | NUC 课网 `ping` | ath9k；WPA3 |

---

## 3. 无线选型 · **已钉**（PR-N-wifi-0 改钉 iwl · 2026-09-26）

> **契约**：一颗芯片 + 总线 + 课机 + 固件路径；驱动在 wifi-1。

### 3.0 课机无线库存

| 机 | BDF | 芯片 | PCI ID | rev | 驱动族 | 角色 |
| -- | --- | ---- | ------ | --- | ------ | ---- |
| **NUC** | `01:00.0` | Intel Wireless **8265 / 8275** | **`8086:24fd`** | 78 | iwlwifi | **✅ 已钉主路径** |
| N56VZ | `03:00.0` | AR9485 | `168c:0032` | 01 | ath9k | 随后柱 |
| （无） | — | RTL8188EU | `0BDA:…` | — | rtl8xxxu | 无货；USB 骨架软失败可留 |

### 3.1 唯一主路径

| 项 | 已钉 |
| -- | ---- |
| **形态** | NUC **板载** PCIe 无线 |
| **芯片** | Intel Wireless **8265 / 8275** |
| **PCI** | VID **`8086`** DID **`24fd`**（rev 78 · class `0280`） |
| **驱动名** | `iwl8265`（`lsdev`；口语 iwl / iwlwifi） |
| **课机（硬）** | **NUC 必通**；N56 无线不挡本柱收口 |
| **固件** | Guest **`FW/IWL8265.UCODE`**（≈2.3 MiB；自 `iwlwifi-8265-36.ucode`） |
| **关联** | wifi-2：WPA2-PSK 主；open 备选 |
| **上栈** | 以太网帧 → `NetInputFrame` / lwIP；**不** mac80211 |

```text
NUC 01:00.0  Intel 8265/8275 (8086:24fd)
        │  PCIe
   HAL/X64/Drivers/Iwl/     ← wifi-1/2
        │  读 FW/IWL8265.UCODE
   NetAttachNic(NIC_L2)     ← wifi-2
        │
   NetInputFrame / DHCP / ping

N56 AR9485 (168c:0032) ──► 随后柱 ath9k
USB 8188EU             ──► 无货
```

### 3.2 固件摆放（已落盘）

| 项 | 值 |
| -- | -- |
| **路径** | [`ToyImage/RootFs/X64/FW/IWL8265.UCODE`](../../../ToyImage/RootFs/X64/FW/IWL8265.UCODE) |
| **说明** | [`FW/README.txt`](../../../ToyImage/RootFs/X64/FW/README.txt) |
| **来源** | 宿主 `/lib/firmware/intel/iwlwifi/iwlwifi-8265-36.ucode.zst` 解压 |
| **SHA256** | `1336afcd028ed094d1fe33893c84c273bb5711be52970040344a75a12f276d56` |
| **许可** | linux-firmware / Intel 再分发；**非** ToyOS 自研；无文件 → Probe 软失败 |

真机 U 盘：把整个 `FW/` 拷进 TOYOS 卷根（与 `Kernel.elf` 同级）。

### 3.3 明确不选 / 随后

| 候选 | 结论 |
| ---- | ---- |
| USB 8188EU 双机同棒 | 无货；不作硬钉 |
| N56 ath9k 抢本柱 ★ | ❌；**随后柱** |
| ath10k / mt76 | ❌ |
| QEMU 虚拟 Wi‑Fi | ❌ |

### 3.4 风险表

| 风险 | 缓解 |
| ---- | ---- |
| 固件大 / 许可 | 已进 `FW/` + README；wifi-1 只读加载 |
| iwl 状态机重 | MVP：Probe+fw → 扫 AP → PSK/open → ping |
| N56 无线弱 | 有线 alx；后开 ath9k |
| 无 mac80211 | 手写最小 STA；Linux **只读** |

### 3.5 历史备忘

曾钉 USB 8188EU（无货作废）。曾草案 ath9k 优先 → 改 **NUC iwl 优先**（用户确认）。

---

## 4. 目录与命名约定

| 路径 | 说明 |
| ---- | ---- |
| `HAL/X64/Drivers/Iwl/` | **wifi-1 起**；`Iwl.c` / `IwlProbe.c` / `IwlFw.c`… ≤300 |
| `HAL/X64/Drivers/Net/NetIwl.c` | Probe/Bind；wifi-2 再 `NetAttachNic` |
| `HAL/X64/Drivers/Ath9k/` | N56 随后柱 |
| `HAL/X64/Drivers/Wifi/` | 旧 USB 8188EU 骨架；可留软失败，不抢 ★ |

注册：`HalDriverRegister` 增 `IwlDriverRegister`；Probe 失败静默。

---

## 5. 与 USB / 输入（现状）

- NUC：xHCI + I219；无线 **8265 主路径**。  
- N56：EHCI/PS/2 + alx；无线 AR9485 **随后**。

---

## 6. 验收口令（给人）

| 机 | 现象 |
| -- | ---- |
| **NUC** | `Boot: iwl8265` + `lsdev` 有 `iwl8265`；wifi-2：`ping` |
| N56VZ | 有线 alx；无线本柱不挡 |
| 回归 | `./Scripts/smoke-boot.sh` |

---

## 7. 参考（只读）

- Linux `drivers/net/wireless/intel/iwlwifi/`  
- Linux `drivers/net/wireless/ath/ath9k/`（随后）  
- `iwlwifi-8265-36.ucode` / linux-firmware  
- ToyOS `E1000/` + `NetE1000.c`  
- [`已完/I219真机网课路径.md`](../已完/I219真机网课路径.md)
