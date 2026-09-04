#!/bin/bash
# QEMU virt aarch64 验收（PR-V6）
# 自有 Boot：-kernel + DTB loader + ramfb/virtio；不是 ToyImage/run-split.sh / AAVMF。
# 详见 run-virt-common.sh --help 说明（本脚本转发）。
set -e
cd "$(dirname "$0")"
# shellcheck source=run-virt-common.sh
source ./run-virt-common.sh

TOY_VIRT_ARCH=arm64
TOY_VIRT_MAKE_ARCH=arm64
export TOY_VIRT_MAKE_ARCH
TOY_VIRT_ELF="${TOY_VIRT_ELF:-Build/arm64/Kernel.elf}"
TOY_VIRT_QEMU="${QEMU_AARCH64:-qemu-system-aarch64}"
TOY_VIRT_HELLO_PAT='ToyOS Arm64 virt: hello'

toy_virt_main "$@"
