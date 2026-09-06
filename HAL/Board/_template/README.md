# Board：`<board-name>`（模板 — 复制后改名）

> 从 `HAL/Board/_template/` 复制到 `HAL/<Arch>/Board/<board>/`，再填本页。  
> 约定总览：[`../README.md`](../README.md)。概念：[`Documents/启动与板级支持.md`](../../../Documents/启动与板级支持.md)。

| 项 | 填写 |
|----|------|
| **板名** | 例：Milk-V Duo S |
| **Arch** | `Arm64` / `RiscV`（二选一；x86 桌面真机走 **1.3c**，不要用本模板） |
| **固件** | 厂商 U-Boot 版本 / 获取方式（**不改**厂商源码） |
| **加载方式** | `booti` / `bootm` / 其它；内核与 DTB 地址 |
| **串口** | SoC UART 基址、波特率、接线（USB-UART 口） |
| **能力** | 命令行 only？有无 FB？有无块设备？（勾选见 `BoardConfig.h`） |
| **课堂关系** | GUI 仍 QEMU virt；本板 = UART 靶 |

---

## 加载约定（U-Boot）

在厂商 U-Boot 提示符下（变量名按板改）：

```text
load mmc 0:1 ${kernel_addr_r} <kernel-image>
load mmc 0:1 ${fdt_addr_r} <board>.dtb
booti ${kernel_addr_r} - ${fdt_addr_r}
```

入口寄存器（AArch64 常见）：`x0 = DTB 物理地址`，`x1～x3 = 0`。  
Startup 必须保存 DTB，填 `BOOT_INFO` 后调 `KernelMain`。**Common 不解析 DTB。**

---

## 本包文件

| 文件 | 状态 |
|------|------|
| `README.md` | 本文件 |
| `BoardConfig.h` | 从 `BoardConfig.h.example` 复制并改名 |
| `Board.c` / `Board.h` | B2/B3 起实现；Startup 调用 |
| `NOTES.md` | 可选；从 `NOTES.md.example` 复制 |

---

## 验收（本板）

- [ ] 厂商 U-Boot 不改源码即可加载  
- [ ] 串口出现 `ToyOS ready`（或下文写明的等价串）  
- [ ] `BOOT_INFO.Regions` 与 DRAM 大致一致  
- [ ] `git diff`：**Common 无业务改动**（仅交本 Board 包 + 必要时薄 Startup）  
- [ ] **未**在本板做 GUI / 桌面  

等价横幅（若不用默认句，写在这里）：`________________`

---

## 已知缺口

- （例：无 COM 风格调试口；仅 SoC UART）  
- （例：尚无 SD→FAT；本柱不要求）  

---

## 明确不做

- 重写 BootROM / DRAM / 厂商 U-Boot 源码  
- 与 x86 UEFI 桌面真机（**1.3c**）混仓  
- 把板级 `#ifdef` 或 DTB 解析放进 Common  
