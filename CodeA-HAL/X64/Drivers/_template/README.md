# ToyOS 驱动模板

> **PR-MOD-drv-norm**：新设备必须落在 `Drivers/<DeviceName>/`，禁止新增 `Drivers/` 根级业务 `.c`。  
> 规格：[`Documents/待做/模块化与App课堂闭环.md`](../../../../Documents/待做/模块化与App课堂闭环.md) §5.1。  
> 指南：[`Documents/驱动/驱动开发指南.md`](../../../../Documents/驱动/驱动开发指南.md)。

## 写一个 ToyOS 驱动（5 步）

1. 复制 `Template.c` 到 `../<MyDevice>/MyDriver.c`（**不要**留在 `_template/`；**不要**放到 `Drivers/` 根）
2. 改 `Name`、符号前缀（`Template*` / `gTemplate*`）、`MyDriverRegister` 函数名
3. 实现 `Probe`：发现硬件则 `return 0`，否则 `return -1`
4. 在 `HAL/X64/Hal/HalDevices.c` → `HalDriverRegister()` 加一行 `MyDriverRegister();`  
   若 `<MyDevice>/` 是新目录：在顶层 `Makefile` 的 `DRIVER_SRCS` 增加  
   `$(wildcard HAL/$(HAL_ARCH)/Drivers/<MyDevice>/*.c)`
5. `./build.sh && cd ../ToyImage && ./run-split.sh`，Shell 中 `lsdev` 应看到 `my-driver …`

## 三种 Probe 匹配模式

### A. PCI 扫描（如 ahci / nvme / xhci）

```c
/* TODO: 用 PciScan… 扫 VID/DID 或 class */
```

### B. MMIO 扫描（如 virtio-mmio）

```c
/* TODO: 遍历 MMIO 槽，匹配 magic */
```

### C. 固定口 / 固定地址（如 ata-pio / ps2-kbd）

```c
/* TODO: 直接访问 0x1F0 / 0x60 等固定口 */
```

## 三种 Backend 挂载

### Input（键盘鼠标）

`Bind` 中：`ToyDriverInputAttach(&gMyBackend)`  
强绑某 USB/PS2 宿主时：**代码放进该控制器目录**（如 `XHCI/`、`Ps2/`），勿平行再建 `Input/`。

### Block（硬盘）

`Bind` 中：`ToyDriverBlockAttach(&gMyBackend)`  
进 `Ahci/` / `Nvme/` / ATA 夹（见规格 §5.2 债表）。

### Net（网卡）

**新网卡**：实现 `NIC_L2`（`SendFrame`/`Poll`/`GetMac`），Bind 里 **`NetAttachNic(&gMyNicL2)`**；RX 调 `NetInputFrame`。  
**不要**自造整份 `NET_BACKEND`（那是 `Net.c` 协议门面）。  
范例：`../E1000/`。注释骨架：同目录 `TemplateNetL2.c`（**勿**在 `_template/` 内当真驱动改）。  
详见 [`驱动开发指南.md`](../../../../Documents/驱动/驱动开发指南.md) §6.1；过程笔记：[`驱动开发范例-网卡L2.md`](../../../../Documents/驱动/驱动开发范例-网卡L2.md)。

地址/DHCP（协议栈，非 L2 驱动必写）：`net config` / `lwip dhcp` — 见路线图 PR-N-nic 序 4～5。

（历史）仅协议门面：`ToyDriverNetAttach(&gNetBackend)` — 一般不由新人网卡驱动调用。

## 常见错误

- **Probe 返回 0 但 lsdev 看不到**：检查 Bind 是否返回 0
- **Register 了但 lsdev 看不到**：忘了在 `HalDriverRegister()` 调用 `MyDriverRegister()`
- **`_template/` 里的 .c 不编译**：模板永不编入，必须复制到 `Drivers/<Device>/`
- **新目录编不进**：忘了 Makefile `DRIVER_SRCS` wildcard
- **丢在 `Drivers/` 根**：违规（norm）；搬家债见规格 §5.2，勿再增根级业务 `.c`
- **系统卡死**：Probe 中有死循环；加超时
- **网卡 lsdev 有但不通**：未 `NetAttachNic`，或 RX 未 `NetInputFrame`

## 禁止

- 把 `_template/Template.c` 留在 `_template/` 下指望它自动编入
- 修改 `_template/` 内的文件当作真驱动用
- **新增** `HAL/*/Drivers/*.c` 根级业务实现（Demo/Serial 遗留除外，由后续刀处置）
- 让 Probe 中的硬件访问死循环
- 新网卡在 `Net.c` 加 `if (gMyNic)` 特例

## Demo / Serial（norm 定调）

| 文件 | 处置 |
| ---- | ---- |
| `DemoDriver.c` | **保持根扁平**（课堂开关用）；或旁置 `_template/` 旁说明——**本刀不搬** |
| `Serial.c` | 目标独立 `Serial/`（`drv-block`/`misc` 前不强迫）；新串口逻辑勿再堆根 |

## 进阶

- 简单参考：`HAL/X64/Drivers/Ps2/InputPs2.c`
- 复杂参考：`HAL/X64/Drivers/XHCI/`（已一夹）
- 网卡 L2：`HAL/X64/Drivers/E1000/` + `Include/DriverNic.h`
- 网卡骨架：同目录 `TemplateNetL2.c`（注释草稿，不链入）
- 设计：`Documents/已完/驱动模板设计.md`
- 完整指南：`Documents/驱动/驱动开发指南.md`
- 过程范例：`Documents/驱动/驱动开发范例-网卡L2.md`
