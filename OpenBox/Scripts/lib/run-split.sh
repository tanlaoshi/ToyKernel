#!/bin/bash
# 双盘 QEMU（唯一推荐入口）— PR-Q1
#   disk0 = Esp/X64/     → ESP/Boot（EFI/BOOT/BOOTX64.EFI）
#   disk1 = RootFs/X64/  → TOYOS 系统盘（Kernel.elf、THEME.CFG、用户 ELF）
#
# 常用：
#   ./Scripts/run-split.sh
#   ./Scripts/run-split.sh --kill-qemu
#   TOY_SMP=1 ./Scripts/run-split.sh
#   ./Scripts/run-split.sh --headless
#   TOY_MEM=2G ./Scripts/run-split.sh        # 默认 1024M（1G）
#   TOY_DISK=ahci ./Scripts/run-split.sh
#   TOY_DISK=nvme ./Scripts/run-split.sh
#   TOY_NET=e1000 ./Scripts/run-split.sh
#   TOY_USB_HUB=1 ./Scripts/run-split.sh
#   TOY_USB_MSC=1 ./Scripts/run-split.sh   # Fixtures/msc-stick/
#   TOY_USB_UHCI=1 ./Scripts/run-split.sh  # piix3-usb-uhci + mouse（PR-H-uhci-1）
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=toyos-common.sh
. "$SCRIPT_DIR/toyos-common.sh"
IMAGE_ROOT="$(toyos_image_root "$0")"
cd "$IMAGE_ROOT"

. "$SCRIPT_DIR/dock-icon.sh"
install_toyos_dock_icon || true
# shellcheck source=toy-qemu-lib.sh
. "$SCRIPT_DIR/toy-qemu-lib.sh"

toy_qemu_parse_args "$@"
"$SCRIPT_DIR/prepare-rootfs.sh"

toy_qemu_read_theme_mode RootFs/X64/THEME.CFG
toy_qemu_setup_ovmf
toy_qemu_prepare_smp

toy_qemu_stash_boot_payloads
trap 'toy_qemu_restore_boot_payloads' EXIT

DISPLAY_ARGS=(-display gtk,zoom-to-fit=off)
if [ "${TOY_HEADLESS:-0}" = 1 ]; then
    DISPLAY_ARGS=(-display none)
    toy_qemu_info "qemu: headless (-display none)"
fi

USB_ARGS=()
UHCI_ARGS=()
if [ "${TOY_USB_UHCI:-0}" = 1 ]; then
    # i440fx：piix3-usb-uhci；挂一只鼠造 CCS≠0
    UHCI_ARGS=(-device piix3-usb-uhci,id=uhci -device usb-mouse,bus=uhci.0)
    toy_qemu_info "qemu: TOY_USB_UHCI=1 (piix3-usb-uhci + usb-mouse)"
fi
if [ "${TOY_USB_HUB:-0}" = 1 ]; then
    USB_ARGS=(-device usb-hub,bus=xhci.0,port=1 -device usb-kbd,bus=xhci.0,port=1.1 -device usb-tablet,bus=xhci.0,port=2)
    toy_qemu_info "qemu: TOY_USB_HUB=1 (kbd behind hub port 1.1)"
else
    USB_ARGS=(-device usb-kbd,bus=xhci.0 -device usb-tablet,bus=xhci.0)
fi

# PR-H-usb-uart：QEMU usb-serial 实为 FTDI（VID 0x0403），不是 CDC-ACM。
# 用于课堂验 FTDI TX tee；CDC 认领仅当真机/仿真出现 0x02/0x0A 描述符时。
CDC_CHARDEV_ARGS=()
if [ "${TOY_USB_SERIAL:-0}" = 1 ]; then
    TOY_USB_SERIAL_LOG="${TOY_USB_SERIAL_LOG:-/tmp/toy-usb-uart-cdc.log}"
    : >"$TOY_USB_SERIAL_LOG"
    CDC_CHARDEV_ARGS=(
        -chardev "file,id=toycdc,path=${TOY_USB_SERIAL_LOG},append=on"
        -device usb-serial,bus=xhci.0,chardev=toycdc
    )
    toy_qemu_info "qemu: TOY_USB_SERIAL=1 (usb-serial=FTDI → $TOY_USB_SERIAL_LOG)"
fi

MSC_DISK_ARGS=()
if [ "${TOY_USB_MSC:-0}" = 1 ]; then
    if [ ! -d Fixtures/msc-stick ] || [ ! -f Fixtures/msc-stick/TOYOS.ID ]; then
        echo "error: TOY_USB_MSC=1 needs Fixtures/msc-stick/TOYOS.ID" >&2
        exit 1
    fi
    toy_qemu_info "qemu: TOY_USB_MSC=1 (usb-storage ← Fixtures/msc-stick/)"
    MSC_DISK_ARGS=(
        -drive if=none,id=toymsc,format=raw,file=fat:rw:Fixtures/msc-stick
        -device usb-storage,drive=toymsc,bus=xhci.0
    )
fi

NETDEV_ARGS=(user,id=n0)
if [ "${TOY_NO_HOSTFWD:-0}" != 1 ]; then
    NETDEV_ARGS=(user,id=n0,hostfwd=udp::5555-:5555,hostfwd=tcp::2222-:7,hostfwd=tcp::9000-:9000)
fi

# bootindex：干净 NVRAM 时仍优先从 ESP 找 \EFI\BOOT\BOOTX64.EFI
DISK_ARGS=()
case "${TOY_DISK:-ide}" in
    ahci|AHCI)
        toy_qemu_info "qemu: disk=ahci (PR-H1)"
        DISK_ARGS=(
            -device ich9-ahci,id=ahci
            -drive if=none,id=toyesp,format=raw,file=fat:rw:Esp/X64
            -device ide-hd,drive=toyesp,bus=ahci.0,bootindex=0
            -drive if=none,id=toyroot,format=raw,file=fat:rw:RootFs/X64
            -device ide-hd,drive=toyroot,bus=ahci.1,bootindex=1
        )
        ;;
    nvme|NVMe|NVME)
        toy_qemu_info "qemu: disk=nvme (PR-H5)"
        DISK_ARGS=(
            -drive if=none,id=toyesp,format=raw,file=fat:rw:Esp/X64
            -device nvme,serial=toyesp,drive=toyesp,logical_block_size=512,physical_block_size=512,bootindex=0
            -drive if=none,id=toyroot,format=raw,file=fat:rw:RootFs/X64
            -device nvme,serial=toyroot,drive=toyroot,logical_block_size=512,physical_block_size=512,bootindex=1
        )
        ;;
    *)
        DISK_ARGS=(
            -drive if=none,id=toyesp,format=raw,file=fat:rw:Esp/X64
            -device ide-hd,drive=toyesp,bus=ide.0,unit=0,bootindex=0
            -drive if=none,id=toyroot,format=raw,file=fat:rw:RootFs/X64
            -device ide-hd,drive=toyroot,bus=ide.0,unit=1,bootindex=1
        )
        ;;
esac

# 镜像勿放在 Esp vvfat 内
if [ "${TOY_INSTALL_DISK:-0}" = 1 ]; then
    if [ "${TOY_DISK:-ide}" != "ahci" ] && [ "${TOY_DISK:-ide}" != "AHCI" ]; then
        echo "error: TOY_INSTALL_DISK=1 requires TOY_DISK=ahci (third AHCI port)" >&2
        exit 1
    fi
    INSTALL_IMG="${TOY_INSTALL_IMG:-/tmp/toyos-install-target.img}"
    INSTALL_MIB="${TOY_INSTALL_MIB:-512}"
    if [ ! -f "$INSTALL_IMG" ]; then
        toy_qemu_info "qemu: create $INSTALL_IMG (${INSTALL_MIB}MiB)"
        qemu-img create -f raw "$INSTALL_IMG" "${INSTALL_MIB}M" >/dev/null
    fi
    toy_qemu_info "qemu: TOY_INSTALL_DISK=1 → $INSTALL_IMG on ahci.2 (Guest drive 2)"
    DISK_ARGS+=(
        -drive if=none,id=toyinst,format=raw,file="$INSTALL_IMG"
        -device ide-hd,drive=toyinst,bus=ahci.2
    )
fi

NET_ARGS=()
case "${TOY_NET:-virtio}" in
    e1000e|E1000E)
        toy_qemu_info "qemu: net=e1000e (PR-H4e-1)"
        NET_ARGS=(-device e1000e,netdev=n0)
        ;;
    e1000|E1000)
        toy_qemu_info "qemu: net=e1000 (PR-H4)"
        NET_ARGS=(-device e1000,netdev=n0)
        ;;
    *)
        NET_ARGS=(-device virtio-net-pci,netdev=n0)
        ;;
esac

NO_REBOOT_ARGS=()
if [ "${TOY_HEADLESS:-0}" = 1 ] || [ "${TOY_NO_REBOOT:-0}" = 1 ]; then
    NO_REBOOT_ARGS=(-no-reboot)
fi

MEM="${TOY_MEM:-1024M}"
toy_qemu_info "qemu: mem=${MEM}"

qemu-system-x86_64 \
    -name "ToyOS",process=qemu-system-x86_64 \
    -drive if=pflash,format=raw,readonly=on,file="$CODE" \
    -drive if=pflash,format=raw,file=Fw/OVMF_VARS.fd \
    "${DISK_ARGS[@]}" \
    -m "$MEM" \
    -smp "$TOY_SMP" \
    -device VGA,edid=on,xres="${TOY_QEMU_XRES}",yres="${TOY_QEMU_YRES}" \
    "${DISPLAY_ARGS[@]}" \
    -device qemu-xhci,id=xhci \
    "${USB_ARGS[@]}" \
    "${UHCI_ARGS[@]}" \
    "${CDC_CHARDEV_ARGS[@]}" \
    "${MSC_DISK_ARGS[@]}" \
    -netdev "${NETDEV_ARGS[@]}" \
    "${NET_ARGS[@]}" \
    -serial stdio \
    "${NO_REBOOT_ARGS[@]}"
