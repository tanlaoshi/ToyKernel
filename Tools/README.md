# ToyKernel/Tools

目录按 [`路线图 · 五、C 标识符`](../Documents/路线图.md#五c-标识符命名规划) 用 **PascalCase**；**脚本一律 kebab-case**（`build-sdk.sh`、`run-virt-*.sh`）。  
Unix CRT 头（`include/stdio.h`）与链接脚本 `user.ld` 仍保持小写惯例。

## 用户态 SDK（PR-A-sdk-pack）

```bash
./Tools/build-sdk.sh          # → Dist/ToySdk/ + Dist/ToySdk.tar.gz（gitignored）
# 应用侧也可：tar xzf Dist/ToySdk.tar.gz -C /tmp && make -C /tmp/ToySdk/Examples/Hello
```

打包进 SDK 的说明按「解压到任意目录」写：[`Sdk/README.md`](Sdk/README.md)。上手：[`应用开发指南.md`](../Documents/应用开发指南.md)。

---

## 交叉工具链（PR-A6，不入库）

本目录也用于本机没有 `gcc-aarch64-linux-gnu` / `gcc-riscv64-linux-gnu` 时的本地工具链。  
**家 ↔ 公司如何同步进度与工具链**：见 [`路线图.md` · 家↔公司同步](../Documents/路线图.md#三家--公司同步)；用户程序链 CRT 调研见 [`技术手册 · PR-V3`](../Documents/技术手册.md#7-交叉工具链调研pr-v3)。

## 目录约定

- `Tarballs/` — 下载的压缩包
- `Extract/` — 解压后的 xPack 工具链（Makefile 会自动探测）
- `Debs/` / `Root/` — 可选：与本机同版本的 QEMU deb 及解压树（`run-virt-*.sh` 可选用）
- `Qemu/` — 占位 / 本地用

若本机仍是旧路径 `tools/extract`，请改名为 `Tools/Extract`（其余 `tarballs`→`Tarballs`、`root`→`Root` 同理）。

## 获取工具链

```bash
mkdir -p Tools/Tarballs Tools/Extract
# RISC-V
wget -O Tools/Tarballs/riscv-gnu.tar.gz \
  'https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v13.2.0-2/xpack-riscv-none-elf-gcc-13.2.0-2-linux-x64.tar.gz'
tar -xzf Tools/Tarballs/riscv-gnu.tar.gz -C Tools/Extract
# AArch64
wget -O Tools/Tarballs/arm-gnu.tar.gz \
  'https://github.com/xpack-dev-tools/aarch64-none-elf-gcc-xpack/releases/download/v13.2.1-1.1/xpack-aarch64-none-elf-gcc-13.2.1-1.1-linux-x64.tar.gz'
tar -xzf Tools/Tarballs/arm-gnu.tar.gz -C Tools/Extract
```

或系统包：`apt install gcc-aarch64-linux-gnu gcc-riscv64-linux-gnu qemu-system-arm qemu-system-misc`

## 运行

```bash
./build.sh arm64 && ./run-virt-arm.sh
./build.sh riscv && ./run-virt-riscv.sh   # 内部加 -bios none
```
