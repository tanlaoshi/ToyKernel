#!/bin/bash
# PR-Q1：无头冒烟 — 清残留 QEMU、默认单核、等到串口出现 ToyOS ready
set -eu
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=toyos-common.sh
. "$SCRIPT_DIR/toyos-common.sh"
IMAGE_ROOT="$(toyos_image_root "$0")"
cd "$IMAGE_ROOT"

export TOY_KILL_QEMU=1
export TOY_HEADLESS=1
export TOY_NO_HOSTFWD=1
export TOY_SMP="${TOY_SMP:-1}"
TIMEOUT_SEC="${SMOKE_TIMEOUT:-90}"
LOG="${SMOKE_LOG:-/tmp/toyos-smoke-$$.log}"

cleanup() {
    pkill -9 -f 'qemu-system-x86_64.*ToyOS' 2>/dev/null || \
        pkill -9 -f 'qemu-system-x86_64' 2>/dev/null || true
    if [ -n "${QEMU_PID:-}" ] && kill -0 "$QEMU_PID" 2>/dev/null; then
        kill -9 "$QEMU_PID" 2>/dev/null || true
        wait "$QEMU_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

if [ ! -f RootFs/X64/Kernel.elf ]; then
    echo "error: missing RootFs/X64/Kernel.elf — build ToyKernel first" >&2
    exit 1
fi

echo "smoke: TOY_SMP=${TOY_SMP} TOY_DISK=${TOY_DISK:-ide} TOY_NET=${TOY_NET:-virtio} TOY_USB_HUB=${TOY_USB_HUB:-0} TOY_USB_MSC=${TOY_USB_MSC:-0} TOY_USB_SERIAL=${TOY_USB_SERIAL:-0} TOY_USB_UHCI=${TOY_USB_UHCI:-0} timeout=${TIMEOUT_SEC}s log=${LOG}"
rm -f "$LOG"
: >"$LOG"
"$SCRIPT_DIR/run-split.sh" --kill-qemu --headless --smp="${TOY_SMP}" >"$LOG" 2>&1 &
QEMU_PID=$!

i=0
while [ "$i" -lt "$TIMEOUT_SEC" ]; do
    # 去掉 CR，避免某些 grep 把串口日志当怪异文本
    # PR-I18N2：就绪串可中/英（lang=zh →「ToyOS 就绪」）
    if tr -d '\r' <"$LOG" 2>/dev/null | grep -E 'ToyOS ready|ToyOS 就绪' >/dev/null 2>&1; then
        echo "smoke: PASS — found ToyOS ready/就绪"
        if [ "${TOY_DISK:-ide}" = "ahci" ] || [ "${TOY_DISK:-}" = "AHCI" ]; then
            if tr -d '\r' <"$LOG" | grep -F 'Boot: AHCI Drives=' >/dev/null 2>&1; then
                echo "smoke: PASS — AHCI backend (PR-H1)"
                tr -d '\r' <"$LOG" | grep -F 'Boot: AHCI Drives=' | tail -1 || true
            else
                echo "smoke: FAIL — TOY_DISK=ahci but no Boot: AHCI line" >&2
                tr -d '\r' <"$LOG" | grep -E 'ahci|block:|ata' | tail -20 >&2 || true
                exit 1
            fi
        fi
        if [ "${TOY_DISK:-ide}" = "nvme" ] || [ "${TOY_DISK:-}" = "NVME" ] || [ "${TOY_DISK:-}" = "NVMe" ]; then
            if tr -d '\r' <"$LOG" | grep -F 'Boot: NVMe Drives=' >/dev/null 2>&1; then
                echo "smoke: PASS — NVMe backend (PR-H5)"
                tr -d '\r' <"$LOG" | grep -F 'Boot: NVMe Drives=' | tail -1 || true
            else
                echo "smoke: FAIL — TOY_DISK=nvme but no Boot: NVMe line" >&2
                tr -d '\r' <"$LOG" | grep -E 'nvme|block:|ata' | tail -20 >&2 || true
                exit 1
            fi
        fi
        tr -d '\r' <"$LOG" | grep -F 'Smp: APs Started=' | tail -1 || true
        tr -d '\r' <"$LOG" | grep -F 'Smp: Continue Single-CPU' | tail -1 || true
        # PR-H2：课堂 QEMU 应有 USB 键盘 ready（真机可能是 PS2）
        if tr -d '\r' <"$LOG" | grep -E 'Boot: XHCI-HID Keyboard|Boot: PS2-KBD Keyboard' >/dev/null 2>&1; then
            echo "smoke: PASS — keyboard backend (PR-H2)"
            tr -d '\r' <"$LOG" | grep -E 'Boot: XHCI-HID Keyboard|Boot: PS2-KBD Keyboard' | tail -1 || true
        else
            echo "smoke: WARN — no Boot: *Keyboard line (headless may still PASS)" >&2
        fi
        # PR-H-ps2-aux：QEMU i8042 常有 Aux；真机触控板另手测
        if tr -d '\r' <"$LOG" | grep -F 'Boot: PS2-AUX Mouse' >/dev/null 2>&1; then
            echo "smoke: PASS — PS/2 Aux mouse (PR-H-ps2-aux)"
            tr -d '\r' <"$LOG" | grep -F 'Boot: PS2-AUX' | tail -1 || true
        elif tr -d '\r' <"$LOG" | grep -F 'Boot: PS2-KBD Keyboard' >/dev/null 2>&1; then
            echo "smoke: WARN — PS2-KBD ok but no PS2-AUX (ok if no Aux)" >&2
        fi
        # QEMU 可见 MSI（课堂 dual 形）；真机 base=poll，dual 另刀
        if tr -d '\r' <"$LOG" | grep -E 'Boot: XHCI IRQ=MSI' >/dev/null 2>&1; then
            echo "smoke: PASS — xhci irq=msi (QEMU; real-PC H-xhci-base then dual)"
        elif tr -d '\r' <"$LOG" | grep -E 'Boot: XHCI IRQ=(IOAPIC|POLL|ioapic|poll)' >/dev/null 2>&1; then
            echo "smoke: WARN — xhci irq fallback (not msi)" >&2
            tr -d '\r' <"$LOG" | grep -E 'Boot: XHCI IRQ=' | tail -1 || true
        else
            echo "smoke: WARN — no Boot: XHCI IRQ= line" >&2
        fi
        if [ "${TOY_USB_HUB:-0}" = 1 ]; then
            if tr -d '\r' <"$LOG" | grep -F 'Boot: XHCI-HID Via Hub' >/dev/null 2>&1; then
                echo "smoke: PASS — hub keyboard (PR-H-hub)"
            else
                echo "smoke: FAIL — TOY_USB_HUB=1 but no Boot: XHCI-HID Via Hub" >&2
                tr -d '\r' <"$LOG" | grep -E 'xhci|hub' | tail -30 >&2 || true
                exit 1
            fi
        fi
        # PR-H-msc-8：TOY_USB_MSC=1 须见 auto mux（7b）；键鼠仍在
        if [ "${TOY_USB_MSC:-0}" = 1 ]; then
            if tr -d '\r' <"$LOG" | grep -F 'Boot: MSC Auto Mux OK' >/dev/null 2>&1; then
                echo "smoke: PASS — msc auto mux (PR-H-msc-8)"
                tr -d '\r' <"$LOG" | grep -F 'Boot: MSC Auto Mux OK' | tail -1 || true
            else
                echo "smoke: FAIL — TOY_USB_MSC=1 but no Boot: MSC Auto Mux OK" >&2
                tr -d '\r' <"$LOG" | grep -E 'msc auto|msc claim|msc bot|block-mux' | tail -40 >&2 || true
                exit 1
            fi
            if ! tr -d '\r' <"$LOG" | grep -E 'Boot: XHCI-HID Keyboard|Boot: PS2-KBD Keyboard' >/dev/null 2>&1; then
                echo "smoke: FAIL — msc smoke lost keyboard backend" >&2
                exit 1
            fi
        fi
        # PR-H-usb-uart：TOY_USB_SERIAL=1 → QEMU usb-serial(=FTDI) 认领 + chardev TX
        if [ "${TOY_USB_SERIAL:-0}" = 1 ]; then
            CDC_LOG="${TOY_USB_SERIAL_LOG:-/tmp/toy-usb-uart-cdc.log}"
            if tr -d '\r' <"$LOG" | grep -F 'boot: usb-uart ftdi' >/dev/null 2>&1; then
                echo "smoke: PASS — usb-uart ftdi via QEMU usb-serial"
            else
                echo "smoke: FAIL — TOY_USB_SERIAL=1 but no boot: usb-uart ftdi" >&2
                tr -d '\r' <"$LOG" | grep -iE 'usb-uart|ftdi|cdc' | tail -20 >&2 || true
                exit 1
            fi
            if tr -d '\r' <"$CDC_LOG" 2>/dev/null | grep -F 'boot: usb-uart ftdi' >/dev/null 2>&1 || \
               tr -d '\r' <"$CDC_LOG" 2>/dev/null | grep -F 'ToyOS ready' >/dev/null 2>&1; then
                echo "smoke: PASS — usb-uart TX on usb-serial chardev"
            else
                echo "smoke: FAIL — usb-serial chardev has no tee output ($CDC_LOG)" >&2
                tr -d '\r' <"$CDC_LOG" 2>/dev/null | tail -40 >&2 || true
                exit 1
            fi
        fi
        # PR-H-uhci-1：TOY_USB_UHCI=1 → piix3-usb-uhci + mouse → Boot: UHCI CCS
        if [ "${TOY_USB_UHCI:-0}" = 1 ]; then
            if tr -d '\r' <"$LOG" | grep -E 'Boot: UHCI#|Boot: UHCI CCS' >/dev/null 2>&1; then
                echo "smoke: PASS — UHCI probe/CCS (PR-H-uhci-1)"
                tr -d '\r' <"$LOG" | grep -E 'Boot: UHCI#|Boot: UHCI CCS' | tail -3 || true
            else
                echo "smoke: FAIL — TOY_USB_UHCI=1 but no Boot: UHCI line" >&2
                tr -d '\r' <"$LOG" | grep -iE 'uhci|UHCI' | tail -20 >&2 || true
                exit 1
            fi
        fi
        # PR-H3：默认应有 COM1（QEMU）；与 Boot 同构横幅 ToyKernel / COM1 Serial OK
        if tr -d '\r' <"$LOG" | grep -F 'COM1 Serial OK' >/dev/null 2>&1; then
            echo "smoke: PASS — COM1 serial (PR-H3)"
        elif tr -d '\r' <"$LOG" | grep -F '[COM1]:初始化OK' >/dev/null 2>&1; then
            echo "smoke: PASS — COM1 serial (PR-H3, legacy zh)"
        elif tr -d '\r' <"$LOG" | grep -F 'COM1 Serial OK' >/dev/null 2>&1; then
            echo "smoke: PASS — COM1 serial (PR-H3, legacy)"
        fi
        # PR-H4 / H4e-1…3：TOY_NET=e1000|e1000e
        if [ "${TOY_NET:-virtio}" = "e1000e" ] || [ "${TOY_NET:-}" = "E1000E" ]; then
            if tr -d '\r' <"$LOG" | grep -E 'Boot: E1000E($|[^a-zA-Z0-9_])' >/dev/null 2>&1 || \
               tr -d '\r' <"$LOG" | grep -F 'Boot: E1000E' >/dev/null 2>&1; then
                echo "smoke: PASS — e1000e backend (PR-H4e-1)"
                if tr -d '\r' <"$LOG" | grep -F 'Boot: E1000E IRQ=MSI' >/dev/null 2>&1; then
                    echo "smoke: PASS — e1000e irq=msi (PR-H4e-3)"
                else
                    echo "smoke: FAIL — TOY_NET=e1000e but no Boot: E1000E IRQ=MSI" >&2
                    tr -d '\r' <"$LOG" | grep -E 'e1000|MSI|irq' | tail -20 >&2 || true
                    exit 1
                fi
            else
                echo "smoke: FAIL — TOY_NET=e1000e but no Boot: E1000E line" >&2
                tr -d '\r' <"$LOG" | grep -E 'e1000|net:|virtio|link' | tail -20 >&2 || true
                exit 1
            fi
        elif [ "${TOY_NET:-virtio}" = "e1000" ] || [ "${TOY_NET:-}" = "E1000" ]; then
            if tr -d '\r' <"$LOG" | grep -E 'Boot: E1000($|[^eE])' >/dev/null 2>&1 || \
               tr -d '\r' <"$LOG" | grep -F 'Boot: E1000 IRQ=MSI' >/dev/null 2>&1; then
                echo "smoke: PASS — e1000 backend (PR-H4)"
            else
                echo "smoke: FAIL — TOY_NET=e1000 but no Boot: E1000 line" >&2
                tr -d '\r' <"$LOG" | grep -E 'e1000|net:|virtio|link' | tail -20 >&2 || true
                exit 1
            fi
        fi
        exit 0
    fi
    if ! kill -0 "$QEMU_PID" 2>/dev/null; then
        wait "$QEMU_PID" || true
        echo "smoke: FAIL — QEMU exited before ready" >&2
        tail -n 40 "$LOG" >&2 || true
        exit 1
    fi
    i=$((i + 1))
    sleep 1
done

echo "smoke: FAIL — timeout waiting for ToyOS ready" >&2
tail -n 60 "$LOG" >&2 || true
exit 1
