# Board：`virt`（QEMU aarch64 virt）

> 课堂默认板包（**PR-B2**）。共享 virtio/ramfb/DTB 在 [`HAL/Virt/`](../../../Virt/)，不在本目录。  
> 约定总览：[`HAL/Board/README.md`](../../../Board/README.md)。

| 项 | 填写 |
|----|------|
| **板名** | QEMU `virt`（aarch64） |
| **Arch** | `Arm64` |
| **固件** | 无厂商 U-Boot；QEMU `-kernel` + DTB loader（见 `run-virt-arm.sh`） |
| **加载方式** | QEMU ELF `-kernel`；DTB 经 `-device loader` @ `0x4a000000` |
| **串口** | PL011 @ `0x09000000`（`BoardConfig.h` / `HalSerial`） |
| **能力** | 可有 ramfb FB → 桌面子集；`--serial` / 无 FB → 串口壳 |
| **课堂关系** | GUI 课堂靶；真机命令行板另交新包（B3 Duo S） |

---

## 加载约定（QEMU）

```bash
./build.sh arm64                 # 或 BOARD=virt（默认）
./run-virt-arm.sh --headless
```

入口：`Startup.S` → `StartupMain`；DTB → `BOOT_INFO` → `KernelMain`。

---

## 本包文件

| 文件 | 状态 |
|------|------|
| `README.md` | 本文件 |
| `BoardConfig.h` | UART / 能力勾选 |
| `Board.h` / `Board.c` | 板名骨架（B3 再扩 Startup 填充） |

---

## 验收

- [x] `BOARD=virt`（默认）构建不回归
- [x] `make boards ARCH=arm64` 列出本包
- [x] `./smoke-virt.sh`（含 Arm64）
- [ ] 真机 U-Boot（不适用本板）
