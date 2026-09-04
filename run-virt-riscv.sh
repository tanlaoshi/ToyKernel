#!/bin/bash
# QEMU virt riscv64 验收（PR-V6）
# 自有 Boot：OpenSBI + -kernel；不是 ToyImage/run-split.sh / RiscVVirt EDK2。
set -e
cd "$(dirname "$0")"
# shellcheck source=run-virt-common.sh
source ./run-virt-common.sh

TOY_VIRT_ARCH=riscv
TOY_VIRT_MAKE_ARCH=riscv
export TOY_VIRT_MAKE_ARCH
TOY_VIRT_ELF="${TOY_VIRT_ELF:-Build/riscv/Kernel.elf}"
TOY_VIRT_QEMU="${QEMU_RISCV64:-qemu-system-riscv64}"
TOY_VIRT_HELLO_PAT='ToyOS RiscV virt: hello'

toy_virt_main "$@"
