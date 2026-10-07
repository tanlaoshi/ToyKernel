# ToyOS（新树 · 迁移中）

> **本目录是 `~/ToyOSNew` 骨架的文档镜像**（随 ToyKernel 推送）。真迁移树在各机 `~/ToyOSNew`；现网仍在 `~/ToyOS`，**未改、未删**。  
> 大结构方案：[`目录结构-ToyOSNew.md`](../../开发/目录结构-ToyOSNew.md)（权威在 Kernel/Documents/开发；本目录是骨架镜像，便于另一台机对照）  
> **确认方案前：不搬代码、不改旧仓构建。**

## 根下三子目录（故事线）

| 目录 | 人话 | 对应今日（旧仓） |
| ---- | ---- | ---------------- |
| [`Boot/`](Boot/) | 引导 / 固件侧 | `ToyOS/ToyBoot`（+ 其构建产物） |
| [`Kernel/`](Kernel/) | 内核与用户态源码、大部分文档 | `ToyOS/ToyKernel`（+ 其构建产物） |
| [`Runtime/`](Runtime/) | 可运行镜像 / 根文件系统 / 刷盘与 QEMU 用料 | `ToyOS/ToyImage` |

根目录**只有**本 `README.md`（再加日后必要的总配置，若需要）。  
**没有**顶层 `Build/`、`Documents/`。

## 状态

- [x] 建空骨架 `Boot` / `Kernel` / `Runtime`  
- [x] 写结构方案文档  
- [ ] 你确认方案  
- [ ] 再开始逐步迁移（另开清单）
