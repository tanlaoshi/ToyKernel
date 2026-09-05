# 写一个 virtio-xxx（PR-D3）

> 目标：按 Drv 类适配加一块 virtio 设备，**不改 Gui / FAT**。框架见 [`驱动框架.md`](驱动框架.md)；排期见路线图 **1.3d**。

本文以现有范例为准：

| 类 | 范例驱动名 | 位置 |
|----|------------|------|
| **Input** | `virtio-input` | `HAL/Arm64|RiscV/VirtioInput.c`（x86 对等范例：`Drivers/InputXhci.c` → `xhci-hid`） |
| **Net** | `virtio-net` / `virtio-net-pci` | `HAL/Arm64|RiscV/VirtioNet.c`；x86 `HAL/X86_64/Drivers/Net.c` |
| **Block**（D2） | `virtio-blk` / `ata-pio` | 已归档，模式相同 |

Common 业务只调用 `HalInput*` / `HalNet*` / `Block*`，**禁止** `#include` 驱动私有头。

---

## 1. 匹配什么

Probe 里自行认设备（同步、无异步总线框架）：

| 总线 | 怎么认 |
|------|--------|
| **virtio-mmio**（Arm/RiscV virt） | `VirtioMmioScan`；`DeviceId`：block=`2`，net=`1`，input=`18`（见 `VirtioMmio.h`） |
| **virtio-pci**（x86 QEMU） | PCI vendor `0x1AF4` + device（如 net `0x1000`）；见 `Net.c` |
| **DTB 兼容串**（后续板包） | 匹配表字段 `TOY_DRIVER.Match` 预留；当前范例多为 `Match = 0`，在 Probe 内扫 |

Input 范例还会读 virtio-input config name（含 `Key` / `Tab`）区分键盘与 tablet。

---

## 2. 实现哪些 ops

### 注册描述符（所有类共通）

```c
static const TOY_DRIVER gMyDriver = {
    .Name  = "virtio-xxx",
    .Class = TOY_DRV_CLASS_INPUT, /* 或 NET / BLOCK */
    .Match = 0,
    .Probe = MyProbe,   /* 有设备 → 0，并可选 *OutPriv；无 → 非 0 */
    .Bind  = MyBind,    /* 挂上类后端 */
    .Remove = MyRemove, /* 可清空 ready 标志 */
};
```

- **Probe**：找 MMIO/PCI、建队列、置 ready；失败返回非 0（不 Bind）。
- **Bind**：只做类适配挂接（见下），勿再扫总线。
- 调用约定：**同步**；无电源管理 / 完整 DMA API。

### Input 类 → `ToyDrvInputAttach`

`INPUT_BACKEND`（`Include/DrvInput.h`）：

| 字段 | 含义 |
|------|------|
| `Poll` | 抽队列 / 事件 |
| `KeyboardDequeue` | 出队 `HAL_KEYBOARD_REPORT`（有则 1） |
| `KeyboardSetLeds` | 可选；`NULL` → `HalKeyboardSetLeds` 返回 -1 |
| `MousePresent` / `MouseDequeue` | 鼠标/tablet |

### Net 类 → `ToyDrvNetAttach`

`NET_BACKEND`（`Include/DrvNet.h`）：`Ready` / `Poll` / `GetMac` / `GetIp` / `FormatIp` / `ParseIp` / `Ping` / `GetStats` / `SendIp` / `Checksum` / `SetLwIpRx`。

无卡时 Probe 应失败；`HalNetInit` 仍返回 0，避免拖垮 `net` 模块。

### Block 类（对照）→ `ToyDrvBlockAttach`

见 D2：`BLOCK_BACKEND` 的 `Probe` / `ReadSectors` / `WriteSectors`。

---

## 3. 如何 `ToyDrvRegister`

1. 在驱动 `.c` 里提供：

   ```c
   void VirtioXxxRegister(void) {
       (void)ToyDrvRegister(&gMyDriver);
   }
   ```

2. 在本 Arch 的 `HalDrvRegister()` 中调用（与 `VirtioBlkRegister` / `InputXhciRegister` / `NetDrvRegister` 并列）。
3. `KernelModules` 的 `drv` 模块会 `HalDrvRegister()` + `ToyDrvProbeAll()`。
4. 需要 MMIO 映射后再试的类（Block / Input / Net）：在 `HalBlockInit` / `HalUsbInit` / `HalNetInit` 里再 `ToyDrvProbeClass(...)`（已绑定则跳过）。

目录建议：实现放在 `HAL/<Arch>/` 或 `HAL/<Arch>/Drivers/`；**不要**把私有头放进 `Include/`。

---

## 4. 如何验收

### 串口（D3 即可）

启动日志应出现类似：

```text
drv: registered=3 bound=… (+…)
```

x86 典型绑定：`ata-pio` + `xhci-hid` + `virtio-net-pci` → `registered=3 bound=3`。  
virt Arm/RiscV：`virtio-blk` + `virtio-input` + `virtio-net`。

功能冒烟：

```bash
cd ToyKernel && ./build.sh
cd ../ToyImage && ./smoke-boot.sh          # ToyOS ready；键鼠/网仍走 Hal*

cd ToyKernel && ./build.sh arm64
./smoke-virt.sh                            # 或 ./run-virt-arm.sh --headless
# 串口：ping 10.0.2.2；有屏时键鼠可点桌面
```

分层回归：

```bash
rg 'Drivers/' Common    # 应无匹配
```

### `lsdev`（PR-D4）

用户可见枚举（Shell/串口列出已绑定 `TOY_DRIVER.Name`）属 **D4**。D3 用串口 `drv: registered=… bound=…` 与上表功能冒烟即可；D4 落地后按 README 补一行 `lsdev` 验收。

---

## 5. 检查清单（贡献者）

- [ ] `TOY_DRIVER` + `ToyDrvRegister`；类标签正确  
- [ ] Bind 只调 `ToyDrvInputAttach` / `ToyDrvNetAttach` / `ToyDrvBlockAttach`  
- [ ] `HalDrvRegister` 已挂上 Register  
- [ ] Common / Services **未** include 本驱动私有头  
- [ ] 同步 Probe/Bind；无卡不拖垮必需模块（Net）  
- [ ] 串口 `drv:` 计数增加；Input/Net 行为与迁前一致  
