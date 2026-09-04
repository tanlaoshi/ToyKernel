# ToyKernel/tools — 交叉工具链（PR-A6，不入库）

本目录用于本机没有 `gcc-aarch64-linux-gnu` / `gcc-riscv64-linux-gnu` 时的本地工具链。  
**家 ↔ 公司如何同步进度与工具链**：见上级 [`同步说明.md`](../同步说明.md)。

## 目录约定

- `tarballs/` — 下载的压缩包
- `extract/` — 解压后的 xPack 工具链（Makefile 会自动探测）
- `debs/` / `root/` — 可选：与本机同版本的 QEMU deb 及解压树（`run-virt-*.sh` 可选用）

## 获取工具链

```bash
mkdir -p tools/tarballs tools/extract
# RISC-V
wget -O tools/tarballs/riscv-gnu.tar.gz \
  'https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v13.2.0-2/xpack-riscv-none-elf-gcc-13.2.0-2-linux-x64.tar.gz'
tar -xzf tools/tarballs/riscv-gnu.tar.gz -C tools/extract
# AArch64
wget -O tools/tarballs/arm-gnu.tar.gz \
  'https://github.com/xpack-dev-tools/aarch64-none-elf-gcc-xpack/releases/download/v13.2.1-1.1/xpack-aarch64-none-elf-gcc-13.2.1-1.1-linux-x64.tar.gz'
tar -xzf tools/tarballs/arm-gnu.tar.gz -C tools/extract
```

或系统包：`apt install gcc-aarch64-linux-gnu gcc-riscv64-linux-gnu qemu-system-arm qemu-system-misc`

## 运行

```bash
./build.sh arm64 && ./run-virt-arm.sh
./build.sh riscv && ./run-virt-riscv.sh   # 内部加 -bios none
```
