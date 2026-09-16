# ToyOS 驱动框架标准化 — PR 拆分方案

> 阶段 1 分析见 `[驱动框架现状分析.md](驱动框架现状分析.md)`。  
> **决策已确认（2026-09-16）**；同日修订风险/开关/Demo Bind。  
> **PR-D-tpl-0** ✅ TG `5e86cc5`。**PR-D-tpl-1** ✅ TG `4109d5c`。下一刀 **PR-D-tpl-2**。

## 已确认决策


| #   | 项               | 结论                                                                                                                 |
| --- | --------------- | ------------------------------------------------------------------------------------------------------------------ |
| 1   | 模板编译            | `**HAL/X64/Drivers/_template/` 永不编进 Kernel**（只作拷贝源 / 文档骨架）。不得加入 `DRIVER_SRCS` 通配，也不得被递归 wildcard 扫进链接。             |
| 1b  | Demo 源码位置       | 扁平 `HAL/X64/Drivers/DemoDriver.c`（现有 `Drivers/*.c` 通配自动编入）                                                         |
| 2   | 文档落点            | 常驻 `Documents/`：本文件、现状分析、后续 `驱动开发指南.md` / `驱动匹配规范.md` / `驱动模板设计.md`，与路线图并列                                         |
| 3   | Demo 注册         | **默认注册**（`HalDriverRegister` 调 `DemoDriverRegister()`）；可用 `TOY_DEMO_DRIVER=0` 关掉                                   |
| 4   | Demo 编译开关       | `TOY_DEMO_DRIVER` 默认 **1**（`#ifndef` 保护）；传 `-DTOY_DEMO_DRIVER=0` 关闭。勿写裸 `#if TOY_DEMO_DRIVER` 且不定义宏（未定义时求值为 0，会误关） |
| 5   | Demo Bind       | **永远不**调用 `ToyDriverInputAttach`；只占 Driver 实例槽，保证 `lsdev` 可见且不抢真键鼠                                                 |
| 6   | D-tpl-1 `nm` 验收 | **精确符号**列表（见设计稿 §1.2）；裸 `grep -i template` / 对 ELF 扫 `template` 子串**不作为硬失败**                                       |


### 硬约束（追加）

- **模板驱动不要编译进 Kernel**：`_template/Template.c` 及目录内任何 `.c` **禁止**进入 `Kernel.elf`。新人复制出 `MyDriver.c` 后再显式纳入构建与 `*Register()`。
- Demo 是**课堂例外**：为证明 `lsdev` 路径，默认编入并注册；**Bind 永不 Attach Backend**；不改变现有真驱动行为。
- 不改现有 Driver* API 签名；不改 Xhci/Ahci/Nvme/E1000/Virtio* 行为。

## 与用户阶段对照


| 用户阶段   | 建议 PR              | 改动范围                                                 | 验收                                                                                                                    |
| ------ | ------------------ | ---------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------- |
| 1 分析   | ✅                  | `[驱动框架现状分析.md](驱动框架现状分析.md)`                         | 字段与源码一致                                                                                                               |
| 2 设计   | **PR-D-tpl-0** ✅ `5e86cc5` | `[驱动模板设计.md](驱动模板设计.md)`                             | 步骤可执行；写明「模板不链入」；Demo 永不 Attach；精确 `nm`                                                                    |
| 3 模板   | **PR-D-tpl-1** ✅ `4109d5c` | `HAL/X64/Drivers/_template/`（**无 .o 进 Kernel**）      | `./build.sh`；精确 `nm`（设计稿 §1.2）无匹配；README 5 步开篇                                                                |
| 4 Demo | **PR-D-tpl-2**     | `DemoDriver.c` + `HalDriverRegister`（`#ifndef` + 注册） | smoke + `lsdev` 见 `demo-driver input`；串口 `demo: probe/bind called`；NUC 可见且键鼠正常；`-DTOY_DEMO_DRIVER=0` 时无 `demo-driver` |
| 5 指南   | **PR-D-tpl-3**     | `驱动开发指南.md`（≤5 页）+ 路线图/README 入口                     | 含 Demo 与 5 步；强调模板不编进内核；写明勿改 `_template/` 内 .c 就期待编译；链到 `_template/README.md` 与设计稿                                     |
| 6 匹配规范 | **PR-D-tpl-4**（可选） | `驱动匹配规范.md`（只文档）                                     | 三模式 +「不强制」；写明本柱不实现表驱动匹配，`TOY_DRIVER.Match` 仍占位                                                                        |


## 建议 JX 顺序

```
PR-D-tpl-0 设计稿确认
  → PR-D-tpl-1 模板目录（不链入）
  → PR-D-tpl-2 Demo 默认注册 + lsdev（永不 Attach）
  → PR-D-tpl-3 驱动开发指南
  →（可选）PR-D-tpl-4 匹配规范
```

## 明确不做（本柱）

- 改写既有真驱动去填 `Match` 表  
- 实现框架级 PCI/DTB 自动分发（仅文档推荐）  
- 全体驱动改成可加载模块 / 移出 Kernel（超出本柱；仅约束 **模板** 不进 Kernel）  
- 真机 NUC：有条件验收（D-tpl-2 键鼠/lsdev）；QEMU `lsdev` 为必过

