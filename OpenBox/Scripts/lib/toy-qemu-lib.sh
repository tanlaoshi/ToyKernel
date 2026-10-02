# Shared helpers for run-split.sh (NVRAM + args + THEME + SMP)
# shellcheck shell=bash
# 假定调用方已 cd 到 ToyImage 根目录。

# 宿主例行提示：默认安静；TOY_QEMU_VERBOSE=1 才刷
toy_qemu_info() {
    if [ "${TOY_QEMU_VERBOSE:-0}" = 1 ]; then
        echo "$@"
    fi
}

toy_qemu_setup_ovmf() {
    CODE="${OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}"
    VARS_TEMPLATE="${OVMF_VARS_SRC:-/usr/share/OVMF/OVMF_VARS_4M.fd}"
    if [ ! -f "$CODE" ]; then
        CODE=/usr/share/OVMF/OVMF_CODE.fd
    fi
    if [ ! -f "$VARS_TEMPLATE" ]; then
        VARS_TEMPLATE=/usr/share/OVMF/OVMF_VARS.fd
    fi
    if [ ! -f "$CODE" ] || [ ! -f "$VARS_TEMPLATE" ]; then
        echo "error: OVMF not found (install ovmf or set OVMF_CODE / OVMF_VARS_SRC)" >&2
        return 1
    fi
    mkdir -p Fw
    if [ ! -f Fw/OVMF_VARS.fd.clean ]; then
        cp -f "$VARS_TEMPLATE" Fw/OVMF_VARS.fd.clean
    elif [ "$(stat -c%s Fw/OVMF_VARS.fd.clean 2>/dev/null || echo 0)" != \
          "$(stat -c%s "$VARS_TEMPLATE" 2>/dev/null || echo 1)" ]; then
        # CODE_4M 配旧 128K VARS 会丢 BootOrder / 进 EFI Shell
        cp -f "$VARS_TEMPLATE" Fw/OVMF_VARS.fd.clean
        CLEAN_NVRAM=1
    fi
    # 默认保留 NVRAM（BootOrder 等）。需要干净变量存储时：
    #   CLEAN_NVRAM=1 ./Scripts/run-split.sh  或  --clean-nvram
    if [ "${CLEAN_NVRAM:-0}" = 1 ] || [ ! -f Fw/OVMF_VARS.fd ]; then
        cp -f Fw/OVMF_VARS.fd.clean Fw/OVMF_VARS.fd
    elif [ "$(stat -c%s Fw/OVMF_VARS.fd 2>/dev/null || echo 0)" != \
          "$(stat -c%s Fw/OVMF_VARS.fd.clean 2>/dev/null || echo 1)" ]; then
        cp -f Fw/OVMF_VARS.fd.clean Fw/OVMF_VARS.fd
    fi
}

toy_qemu_parse_args() {
    CLEAN_NVRAM="${CLEAN_NVRAM:-0}"
    TOY_KILL_QEMU="${TOY_KILL_QEMU:-0}"
    TOY_HEADLESS="${TOY_HEADLESS:-0}"
    for Arg in "$@"; do
        case "$Arg" in
            --clean-nvram) CLEAN_NVRAM=1 ;;
            --kill-qemu) TOY_KILL_QEMU=1 ;;
            --headless) TOY_HEADLESS=1 ;;
            --smp=*)
                TOY_SMP="${Arg#--smp=}"
                ;;
            --smp)
                echo "error: use --smp=N (e.g. --smp=1)" >&2
                return 1
                ;;
            --force-second)
                ;;
            -h|--help)
                cat <<'EOF'
Usage: ./Scripts/run-split.sh [options]

  Dual-disk QEMU (only supported entry):
    disk0 = Esp/X64/   ESP / Boot (EFI/BOOT/BOOTX64.EFI)
    disk1 = RootFs/X64 TOYOS system volume (Kernel.elf, THEME.CFG, ELFs)

Options:
  --clean-nvram     Reset Fw/OVMF_VARS.fd from clean template
  --kill-qemu       pkill leftover qemu-system-x86_64 before start
  --headless        -display none (CI / smoke; serial still on stdio)
  --smp=N           Pass -smp N (default 2; auto 1 if other QEMU exist)
  -h, --help        This help

Env:
  TOY_SMP=N         Same as --smp=N
  TOY_KILL_QEMU=1   Same as --kill-qemu
  TOY_HEADLESS=1    Same as --headless
  TOY_NO_HOSTFWD=1  Skip hostfwd (smoke/CI; avoids port bind failures)
  TOY_DISK=ahci     Use ich9-ahci instead of IDE (PR-H1 AHCI Block)
  TOY_DISK=nvme     Use PCIe NVMe instead of IDE (PR-H5 NVMe Block)
  TOY_NET=e1000     Use Intel e1000 instead of virtio-net (PR-H4)
  TOY_QEMU_XRES/YRES  Override VGA edid (else RootFs/X64/THEME.CFG mode=)
  CLEAN_NVRAM=1     Same as --clean-nvram
  OVMF_CODE / OVMF_VARS_SRC  Custom firmware paths

Troubleshoot SIPI/AP timeout:
  ./Scripts/run-split.sh --kill-qemu
  TOY_SMP=1 ./Scripts/run-split.sh
EOF
                exit 0
                ;;
        esac
    done
    export CLEAN_NVRAM TOY_KILL_QEMU TOY_HEADLESS
    if [ -n "${TOY_SMP:-}" ]; then
        export TOY_SMP
    fi
}

# 统计其它 qemu-system-x86_64（不含本脚本即将启动的实例）
toy_qemu_count_others() {
    local N
    N="$(pgrep -c -f 'qemu-system-x86_64' 2>/dev/null | head -n1 || true)"
    N="${N:-0}"
    case "$N" in
        ''|*[!0-9]*) N=0 ;;
    esac
    printf '%s' "$N"
}

# 杀掉残留 QEMU，避免 SIPI 饿死 / AP timeout → 访客连环复位
toy_qemu_kill_others() {
    local N
    N="$(toy_qemu_count_others)"
    if [ "$N" -le 0 ]; then
        toy_qemu_info "qemu: no leftover qemu-system-x86_64"
        return 0
    fi
    echo "qemu: killing ${N} leftover qemu-system-x86_64 (SIPI safety)" >&2
    pkill -9 -f 'qemu-system-x86_64' 2>/dev/null || true
    sleep 0.3
}

# 残留实例时默认单核；可选先杀干净
toy_qemu_prepare_smp() {
    local Other
    Other="$(toy_qemu_count_others)"

    if [ "${TOY_KILL_QEMU:-0}" = 1 ]; then
        toy_qemu_kill_others
        Other=0
    elif [ "$Other" -gt 0 ]; then
        echo "warning: ${Other} qemu-system-x86_64 already running — SIPI/AP may timeout." >&2
        echo "warning: re-run with --kill-qemu, or: pkill -9 -f qemu-system-x86_64" >&2
        if [ -z "${TOY_SMP:-}" ]; then
            TOY_SMP=1
            echo "warning: defaulting TOY_SMP=1 while other QEMU exist" >&2
        fi
    fi

    TOY_SMP="${TOY_SMP:-2}"
    export TOY_SMP
    toy_qemu_info "qemu: -smp ${TOY_SMP}"
}

# 从 RootFs/X64/THEME.CFG 读 mode=WxH（系统盘为唯一权威）
toy_qemu_read_theme_mode() {
    local Cfg="${1:-RootFs/X64/THEME.CFG}"
    local Line W H

    TOY_QEMU_XRES="${TOY_QEMU_XRES:-}"
    TOY_QEMU_YRES="${TOY_QEMU_YRES:-}"
    if [ -n "$TOY_QEMU_XRES" ] && [ -n "$TOY_QEMU_YRES" ]; then
        toy_qemu_info "qemu: VGA edid ${TOY_QEMU_XRES}x${TOY_QEMU_YRES} (env override)"
        return 0
    fi
    if [ ! -f "$Cfg" ]; then
        TOY_QEMU_XRES=1920
        TOY_QEMU_YRES=1080
        toy_qemu_info "qemu: VGA edid ${TOY_QEMU_XRES}x${TOY_QEMU_YRES} (default; no $Cfg)"
        return 0
    fi
    Line="$(grep -E '^[[:space:]]*mode=' "$Cfg" | head -1 || true)"
    W="$(printf '%s' "$Line" | sed -n 's/.*mode=\([0-9][0-9]*\)[xX]\([0-9][0-9]*\).*/\1/p')"
    H="$(printf '%s' "$Line" | sed -n 's/.*mode=\([0-9][0-9]*\)[xX]\([0-9][0-9]*\).*/\2/p')"
    if [ -z "$W" ] || [ -z "$H" ]; then
        TOY_QEMU_XRES=1920
        TOY_QEMU_YRES=1080
        toy_qemu_info "qemu: VGA edid ${TOY_QEMU_XRES}x${TOY_QEMU_YRES} (default; no mode= in $Cfg)"
        return 0
    fi
    TOY_QEMU_XRES="$W"
    TOY_QEMU_YRES="$H"
    toy_qemu_info "qemu: VGA edid ${TOY_QEMU_XRES}x${TOY_QEMU_YRES} (from $Cfg)"
}

# ESP 已是 Esp/X64（仅 EFI）；无需再 stash 根目录 ELF
toy_qemu_stash_boot_payloads() {
    return 0
}

toy_qemu_restore_boot_payloads() {
    return 0
}
