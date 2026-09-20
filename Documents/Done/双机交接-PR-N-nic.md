# 双机交接：PR-N-nic（近 12 小时）

> **用途**：公司侧 / 另一台机器 Cursor 同步后，用本文接续工作。  
> **仓库**：`ToyKernel`（独立 git；父仓 edk2 内嵌）。  
> **暗号**：JX = 做文首 ★；TG = 只 commit；TS = push。  
> **写于**：2026-09-18（家侧）。

---

## 1. 当前状态（认 git，不认聊天）

| 项 | 状态 |
| --- | --- |
| 远程 `origin/main` | 已含序 1～6（见下表哈希） |
| 文首 ★ | **PR-N-nic-tray**（序 7） |
| 序 7 tray | **本地已实现、冒烟 PASS，待 TG**（工作区可能仍脏） |
| 本柱收官 | tray TG 后：路线图归档 PR-N-nic；★ 清空或另立 |

同步后先执行：

```bash
cd ToyKernel && git status -sb && git log -6 --oneline
```

若有未提交的 `DesktopNetTray*` / `Desktop*.c` / `Gui*.c` / 路线图改动 → 先手测再 **TG**。

---

## 2. 已入库提交（远程）

| 哈希 | PR | 一句话 |
| --- | --- | --- |
| `adc2979` | 序 1～3 l2/attach/e1000 | `NIC_L2` + `NetAttachNic`；e1000 迁 L2；废 `gNicE1000` |
| `59cd936` | 序 4 addr | `NetConfig`；`net config` / `setip\|setgw\|setdns`；非 HV 不写死 QEMU GW MAC/DNS |
| `581568a` | 序 5 dhcp | `lwip dhcp` / `net dhcp`；8s 软失败；后设静态停 DHCP |
| `6a64e2c` | 序 6 doc | 指南 §5.1；`_template/TemplateNetL2.c`（**不进** Kernel） |

相关路线图锚点：[`路线图.md` · #pr-n-nic](../路线图.md#pr-n-nic)。

---

## 3. 序 7 tray（待 TG）做了什么

- **新文件**：`Common/Services/DesktopNetTray.c`（≤300）
- **行为**：任务栏时钟**左侧**短文案（`net-` / `down` / IPv4）；点击弹简况（ip/gw/dns/link）；与开始菜单互斥；合成路径叠画；标签变化随时钟轮询刷新
- **已改**：`DesktopPaint.c`、`Desktop.c`、`Desktop.h` / `DesktopPrivate.h`、`GuiCompose.c`、`GuiDraw.c`、`GuiPointer.c`、路线图 / 范例文档
- **冒烟**：`./smoke-boot.sh`、`TOY_NET=e1000e ./smoke-boot.sh` → PASS  
- **手测**：任务栏见 IP → 点开合简况 → 开开始菜单无烙印 → **TG**（再按需 **TS**）

---

## 4. 必读入口（勿另起炉灶）

| 文档 / 代码 | 用途 |
| --- | --- |
| [`驱动开发指南.md`](../驱动/驱动开发指南.md) §5.1 | 新人写网卡：仍是 ToyDriver；Bind → `NetAttachNic` |
| [`驱动开发范例-网卡L2.md`](../驱动/驱动开发范例-网卡L2.md) | 本柱过程 / 决策 / 文件地图 |
| [`路线图.md`](../路线图.md#pr-n-nic) | 七刀状态与验收命令 |
| `Include/DriverNic.h` + `NetE1000.c` | L2 契约 + 活范例 |
| `_template/TemplateNetL2.c` | 注释骨架；目录**永不**编进 Kernel |
| `NetConfig` / `LwIpDhcp` | 地址与 DHCP（协议栈侧，不是驱动本体） |

**一句话**：网卡是驱动框架 NET 类成员；本柱补的是 L2 挂钩。禁止新卡在 `Net.c` 加 `if (gMyNic)`，禁止自造胖 `NET_BACKEND`。

---

## 5. 验收备忘

```bash
# TOY_NET 必须是环境变量（不要写成 ./run-split.sh TOY_NET=e1000e）
cd ToyKernel && ./build.sh
cd ../ToyImage && ./smoke-boot.sh
TOY_NET=e1000e ./smoke-boot.sh

# Guest（可选）
show network / net config
lwip on → ping 10.0.2.2
lwip dhcp → 有 offer 时非空 IP；再 setip 后 dhcp=off
# 桌面：任务栏时钟左侧短状态；点击简况
```

共存优先级：DHCP 成功覆盖 `NetConfig`；之后 `net config` / `set*` 停 DHCP 并套静态。

---

## 6. 公司侧建议下一步

1. `git pull`；核对工作区是否含 tray 未提交改动。  
2. 有 tray → 手测 → **TG**（本柱收官）→ 需要时 **TS**。  
3. 路线图：tray 合入后把 `#pr-n-nic` 收进 §七归档；文首 ★ 勿留空悬刀。  
4. **明确不做**（本柱外）：I219 DID 全 quirks、Realtek/无线、完整网络设置器、把 Linux 网卡驱动直接搬进来。

---

## 7. 代码原则（写新文件时）

见路线图 [〇·代码原则](../路线图.md#sec-0-code) 与 [`开发命名规范.md`](../开发/开发命名规范.md) §6.3：

- 新 `.c` **≤300 行**；1 核心 + 多辅助  
- 命名 PascalCase；层前缀最前
