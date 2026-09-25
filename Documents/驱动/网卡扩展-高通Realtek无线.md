# 网卡扩展规划：高通 Atheros / Realtek / 无线

> **状态**：规划稿（2026-09-25）。用户 GO：先有线（AR8161 + Realtek），再无线。  
> **栈契约**：只实现 [`NIC_L2`](../Include/DriverNic.h) → `NetAttachNic`；勿碰 Common 协议 / lwIP。  
> **活范例**：[`驱动开发范例-网卡L2.md`](驱动开发范例-网卡L2.md)（e1000 路径）。  
> **排期正文**：[`路线图.md`](../路线图.md) 排队 [`PR-N-alx-1`](../路线图.md#pr-n-alx-1) 起（input-mux TG 后升 ★）。

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
| **wifi** | PCIe / USB 802.11（选型后钉死一颗） | 演示联网 | 扫 AP + open/WPA2-PSK 择一 + DHCP |

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
| 5 | **PR-N-wifi-0** | 本文 §3 选型钉死；课路径机型 + 芯片 + 总线（USB 或 PCIe）；风险表 | 文档合入；★ 可指向 wifi-1 | 写驱动代码 |
| 6 | **PR-N-wifi-1** | 选定芯片：固件加载 / 探针 / `lsdev`；尚无数据面可先 `HalNetReady=0` | 真机见驱动绑定日志；无卡不挡 | 关联；扫 AP UI |
| 7 | **PR-N-wifi-2** | 扫 AP（Shell 即可）；open **或** WPA2-PSK 择一；DHCP + `ping` | 课网可演示「连上 → ping」 | WPA3；漫游；软 AP |

---

## 3. 无线选型备忘（wifi-0 填写）

| 候选 | 总线 | 优点 | 风险 |
| ---- | ---- | ---- | ---- |
| USB Realtek（8188/8812 类） | USB | 易插拔演示；不绑死笔电 | 需稳定 xHCI；固件许可 |
| ath9k 一代 PCIe | PCIe | 文档多；无复杂 fw | 新机少；N56VZ 多为 ath9k？需 `lsdev`/lspci 确认 |
| ath10k / mt76 | PCIe | 较新 | 固件 + mac80211 过重，**不优先** |

**默认建议（可在 wifi-0 推翻）**：先 **USB Realtek 一颗** 做课堂棒；笔电板载 Wi‑Fi 作加分项。N56VZ 板载无线另查 `lspci` 再决定是否值得并行。

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

## 5. 与 input-mux / USB 鼠

- [`PR-H-input-mux`](../路线图.md#pr-h-input-mux)：N56VZ **PS/2 键已通**；USB 鼠仍废属 **CCS=0**，不阻塞本网卡柱。  
- input-mux **待用户 TG** 后再把文首 ★ 推到 `PR-N-alx-1`（或用户一句 TG 时助手代推）。

---

## 6. 验收口令（给人）

| 机 | 命令 / 现象 |
| -- | ----------- |
| N56VZ | `lsdev` → `alx`；`ping 192.168.x.1`（按家网） |
| Realtek 机 | `lsdev` → `r8169`；同上 |
| 回归 | `./Scripts/smoke-boot.sh`（ToyImage）；默认仍 virtio |
| 无线 | Shell 扫 AP → 关联 → `ping 1.1.1.1` 或网关 |

---

## 7. 参考（只读，勿整文件拷贝）

- Linux `drivers/net/ethernet/atheros/alx/`  
- Linux `drivers/net/ethernet/realtek/r8169*.c`  
- ToyOS `HAL/X64/Drivers/E1000/` + `Net/NetE1000.c`  
- [`已完/I219真机网课路径.md`](../已完/I219真机网课路径.md)（课路径写法模板）
