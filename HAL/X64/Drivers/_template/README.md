# ToyOS 驱动模板

## 写一个 ToyOS 驱动（5 步）

1. 复制 `Template.c` 到 `../MyDriver.c`（**不要**留在 `_template/` 下）
2. 改 `Name`、符号前缀（`Template*` / `gTemplate*`）、`MyDriverRegister` 函数名
3. 实现 `Probe`：发现硬件则 `return 0`，否则 `return -1`
4. 在 `HAL/X64/HalDevices.c` → `HalDriverRegister()` 加一行 `MyDriverRegister();`
5. `./build.sh && cd ../ToyImage && ./run-split.sh`，Shell 中 `lsdev` 应看到 `my-driver input`

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

### Block（硬盘）

`Bind` 中：`ToyDriverBlockAttach(&gMyBackend)`

### Net（网卡）

**新网卡**：实现 `NIC_L2`（`SendFrame`/`Poll`/`GetMac`），Bind 里 **`NetAttachNic(&gMyNicL2)`**；RX 调 `NetInputFrame`。  
**不要**自造整份 `NET_BACKEND`（那是 `Net.c` 协议门面）。  
范例：`NetE1000.c`。注释骨架：同目录 `TemplateNetL2.c`（**勿**在 `_template/` 内当真驱动改）。  
详见 [`驱动开发指南.md`](../../../../Documents/驱动开发指南.md) §5.1；过程笔记：[`驱动开发范例-网卡L2.md`](../../../../Documents/驱动开发范例-网卡L2.md)。

地址/DHCP（协议栈，非 L2 驱动必写）：`net config` / `lwip dhcp` — 见路线图 PR-N-nic 序 4～5。

（历史）仅协议门面：`ToyDriverNetAttach(&gNetBackend)` — 一般不由新人网卡驱动调用。

## 常见错误

- **Probe 返回 0 但 lsdev 看不到**：检查 Bind 是否返回 0
- **Register 了但 lsdev 看不到**：忘了在 `HalDriverRegister()` 调用 `MyDriverRegister()`
- **`_template/` 里的 .c 不编译**：模板永不编入，必须复制到 `Drivers/*.c`
- **系统卡死**：Probe 中有死循环；加超时
- **网卡 lsdev 有但不通**：未 `NetAttachNic`，或 RX 未 `NetInputFrame`

## 禁止

- 把 `_template/Template.c` 留在 `_template/` 下指望它自动编入
- 修改 `_template/` 内的文件当作真驱动用
- 让 Probe 中的硬件访问死循环
- 新网卡在 `Net.c` 加 `if (gMyNic)` 特例

## 进阶

- 简单参考：`HAL/X64/Drivers/InputPs2.c`
- 复杂参考：`HAL/X64/Drivers/InputXhci.c`（含中断）
- 网卡 L2：`HAL/X64/Drivers/NetE1000.c` + `Include/DriverNic.h`
- 网卡骨架：同目录 `TemplateNetL2.c`（注释草稿，不链入）
- 设计：`Documents/Done/驱动模板设计.md`
- 完整指南（PR-D-tpl-3）：`Documents/驱动开发指南.md`
- 过程范例：`Documents/驱动开发范例-网卡L2.md`
