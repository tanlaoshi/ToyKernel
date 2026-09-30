# Net/ — 协议核（非设备 L2）

> **PR-MOD-drv-netglue**：设备 L2 胶水已迁出；本目录只留协议核。

## 留在本目录（协议核）

| 文件 | 职责 |
| ---- | ---- |
| `Net.c` | 发送帧 / 地址查询 / 胖 `NET_BACKEND` |
| `NetArp.c` | ARP / ICMP / ping |
| `NetAddr.c` | IP 文本与查询 |
| `NetRx.c` | 收包与轮询 |
| `NetNic.c` | `NetAttachNic` / L2 分发 |
| `NetPrivate.h` | 协议核 + PCI virtio-net 队列共享私头 |

公开门面仍在 `Drivers/Net.h`（不搬）。

## 已迁出（L2 胶水）

| 原文件 | 目标 |
| ------ | ---- |
| `NetE1000.c` | `E1000/` |
| `NetAlx.c` | `Alx/` |
| `NetRtl.c` | `Rtl/` |
| `NetIwl.c` | `Iwl/` |
| `NetWifi.c` | `Wifi/` |
| `NetVirtio.c` / `NetVirtioStart.c` / `NetDriver.c` | `VirtioNet/`（X64 PCI；**不**并进 `HAL/Virt/`） |

新网卡：设备目录写 `NIC_L2` + `NetAttachNic`，**勿**往本目录塞设备 Probe。
