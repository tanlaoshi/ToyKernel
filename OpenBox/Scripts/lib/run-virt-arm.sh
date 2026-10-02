#!/bin/bash
# QEMU virt aarch64 验收（PR-V6）— 入口在 ToyImage；内核 ../ToyKernel
# 自有 Boot：-kernel + DTB loader + ramfb/virtio；不是 run-split.sh / AAVMF。
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=toyos-common.sh
. "$SCRIPT_DIR/toyos-common.sh"
IMAGE_ROOT="$(toyos_image_root "$0")"
cd "$IMAGE_ROOT"
BOARD=virt
# shellcheck source=run-virt-common.sh
source "$SCRIPT_DIR/run-virt-common.sh"

TOY_VIRT_ARCH=arm64
TOY_VIRT_MAKE_ARCH=arm64
TOY_VIRT_HAL_ARCH=Arm64
export TOY_VIRT_MAKE_ARCH TOY_VIRT_HAL_ARCH
TOY_VIRT_ELF="${TOY_VIRT_ELF:-}"
TOY_VIRT_QEMU="${QEMU_AARCH64:-qemu-system-aarch64}"
TOY_VIRT_HELLO_PAT='ToyOS Arm64 virt: hello'

toy_virt_main "$@"
