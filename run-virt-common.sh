#!/bin/bash
# run-virt-common.sh — PR-V6：Arm/RiscV virt 验收公共逻辑
#
# 这是各 Arch「自有 Boot」的 QEMU virt 验收（-kernel + ramfb/virtio-*），
# 不是 ToyImage/run.sh / run-split.sh 换 arch，也不引入 AAVMF / BOOTAA64.EFI /
# RiscVVirt EDK2。
#
# 由 run-virt-arm.sh / run-virt-riscv.sh source；调用方须先设：
#   TOY_VIRT_ARCH=arm64|riscv
#   TOY_VIRT_QEMU=qemu-system-...
#   TOY_VIRT_ELF=Build/HAL/<Arch>/Kernel.elf
#   TOY_VIRT_MAKE_ARCH=arm64|riscv
#   TOY_VIRT_HAL_ARCH=Arm64|RiscV
#   可选：TOY_VIRT_QEMU_EXTRA=(...)  TOY_VIRT_HELLO_PAT=...
#
# 用法（经包装脚本）：
#   ./run-virt-arm.sh              # 默认：窗口 + 盘 + 输入（交互）
#   ./run-virt-arm.sh --headless   # CI：-nographic 串口冒烟后退出
#   ./run-virt-arm.sh --serial     # 无 ramfb 的 A8 串口子集冒烟
#   ./run-virt-arm.sh --help

toy_virt_usage() {
    cat <<'EOF'
ToyKernel virt 验收（自有 Boot，非 UEFI / 非 ToyImage/run-split.sh）

  ./run-virt-arm.sh | ./run-virt-riscv.sh [选项] [Kernel.elf]

选项：
  （默认）         图形窗口 + virtio-blk + virtio-net + 键鼠；串口 mon:stdio 交互
  --headless       -nographic 冒烟：等 [mod] net / ping 10.0.2.2 / 挂卷后 halt
  --serial         无 ramfb（串口子集模块表；仍无 net）冒烟
  --nodisk         不挂 virtio-blk
  -h, --help       本说明

环境变量：
  TOY_VIRT_MEM=256M|512M     内存
  TOY_VIRT_DISPLAY=gtk|sdl   窗口后端（默认 gtk）
  TOY_VIRT_GUI=1             同默认窗口模式（兼容旧用法）
  TOY_VIRT_SERIAL=1          同 --serial
  TOY_VIRT_NODISK=1          同 --nodisk
  TOY_VIRT_NONET=1           不挂 virtio-net（N10 默认挂 user 网）
  TOY_VIRT_SMP=1|2|N         核数（默认 2；PR-A14；单核路径用 1）
  TOY_RISCV_BIOS_NONE=1      RiscV：-bios none（旧链接对照）

EOF
}

toy_virt_parse_args() {
    TOY_VIRT_MODE=gui
    TOY_VIRT_ELF_ARG=""
    while [ $# -gt 0 ]; do
        case "$1" in
            -h|--help|help)
                toy_virt_usage
                exit 0
                ;;
            --headless|headless|-nographic)
                TOY_VIRT_MODE=headless
                shift
                ;;
            --serial|serial)
                TOY_VIRT_MODE=serial
                shift
                ;;
            --nodisk|nodisk)
                TOY_VIRT_NODISK=1
                shift
                ;;
            --gui|gui)
                TOY_VIRT_MODE=gui
                shift
                ;;
            -*)
                echo "error: unknown option: $1" >&2
                toy_virt_usage >&2
                exit 1
                ;;
            *)
                TOY_VIRT_ELF_ARG="$1"
                shift
                ;;
        esac
    done
    if [ "${TOY_VIRT_SERIAL:-0}" = "1" ]; then
        TOY_VIRT_MODE=serial
    fi
    # 旧用法：默认曾是 headless；TOY_VIRT_GUI=1 开窗。现默认开窗；若显式 GUI=0 且无 --gui 则 headless
    if [ "${TOY_VIRT_GUI:-}" = "0" ] && [ "$TOY_VIRT_MODE" = "gui" ]; then
        TOY_VIRT_MODE=headless
    fi
}

toy_virt_resolve_qemu() {
    if ! command -v "$TOY_VIRT_QEMU" >/dev/null 2>&1; then
        local Base
        Base="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
        if [ -x "$Base/tools/root/usr/bin/$TOY_VIRT_QEMU" ]; then
            TOY_VIRT_QEMU="$Base/tools/root/usr/bin/$TOY_VIRT_QEMU"
        else
            echo "error: $TOY_VIRT_QEMU not found" >&2
            exit 1
        fi
    fi
}

toy_virt_ensure_elf() {
    local Root
    Root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    cd "$Root"
    if [ -n "$TOY_VIRT_ELF_ARG" ]; then
        TOY_VIRT_ELF="$TOY_VIRT_ELF_ARG"
    fi
    if [ ! -f "$TOY_VIRT_ELF" ]; then
        echo "building ARCH=$TOY_VIRT_MAKE_ARCH ..."
        make "ARCH=$TOY_VIRT_MAKE_ARCH" BRINGUP=0
    fi
    if [ ! -f "$TOY_VIRT_ELF" ]; then
        echo "error: missing $TOY_VIRT_ELF" >&2
        exit 1
    fi
    # PR-A12：确保本 arch HELLO.ELF 已构建（prepare 会装入盘）
    local HelloElf="Build/HAL/${TOY_VIRT_HAL_ARCH}/user/hello.elf"
    if [ ! -f "$HelloElf" ]; then
        make "ARCH=$TOY_VIRT_MAKE_ARCH" BRINGUP=0 "$HelloElf"
    fi
}
toy_virt_build_dev_args() {
    MEM="${TOY_VIRT_MEM:-256M}"
    SMP="${TOY_VIRT_SMP:-2}"
    DISP_ARGS=()
    SERIAL_ARGS=()
    DEV_ARGS=(-device virtio-keyboard-device -device virtio-tablet-device)
    BIOS_ARGS=()
    SMP_ARGS=(-smp "$SMP")

    if [ "${TOY_RISCV_BIOS_NONE:-0}" = "1" ] && [ "$TOY_VIRT_ARCH" = "riscv" ]; then
        BIOS_ARGS=(-bios none)
    fi

    if [ "${TOY_VIRT_NODISK:-0}" != "1" ]; then
        export TOY_VIRT_MAKE_ARCH
        ./prepare-virt-rootfs.sh >/dev/null
        # N10：用 raw FAT 镜像，避免 QEMU fat:rw(vvfat) 与 virtio-net 同机 TX 故障
        DEV_ARGS+=(-drive "if=none,id=toyroot,format=raw,file=virt-rootfs.img"
                   -device virtio-blk-device,drive=toyroot)
    fi

    # PR-N10：默认 user 网 + virtio-net-device（MMIO）；TOY_VIRT_NONET=1 可关
    if [ "${TOY_VIRT_NONET:-0}" != "1" ]; then
        DEV_ARGS+=(-netdev user,id=n0 -device virtio-net-device,netdev=n0)
    fi

    case "$TOY_VIRT_MODE" in
        gui)
            if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
                echo "note: no DISPLAY/WAYLAND_DISPLAY — falling back to --headless" >&2
                TOY_VIRT_MODE=headless
            fi
            ;;
    esac

    if [ "$TOY_VIRT_MODE" = "gui" ]; then
        SERIAL_ARGS=(-serial mon:stdio)
        DISP_ARGS=(-device ramfb -display "${TOY_VIRT_DISPLAY:-gtk}")
    elif [ "$TOY_VIRT_MODE" = "headless" ]; then
        SERIAL_ARGS=(-nographic)
        DISP_ARGS=(-device ramfb)
    else
        # serial
        SERIAL_ARGS=(-nographic)
        DISP_ARGS=()
    fi
}

toy_virt_qemu_cmd_prefix() {
    # 打印用
    local NetNote="virtio-net"
    if [ "${TOY_VIRT_NONET:-0}" = "1" ]; then
        NetNote="nonet"
    fi
    echo "virt验收: Arch=$TOY_VIRT_ARCH mode=$TOY_VIRT_MODE smp=${TOY_VIRT_SMP:-2} (自有 Boot，非 ToyImage/run-split.sh)"
    echo "run: $TOY_VIRT_QEMU -M virt -m $MEM -smp ${TOY_VIRT_SMP:-2} ${SERIAL_ARGS[*]} ${DISP_ARGS[*]:-no-fb} + virtio-input/blk/$NetNote"
}

toy_virt_run_interactive() {
    toy_virt_qemu_cmd_prefix
    if [ "$TOY_VIRT_ARCH" = "arm64" ]; then
        local DTB Ec
        DTB=$(mktemp)
        "$TOY_VIRT_QEMU" -M virt,gic-version=2,dumpdtb="$DTB" -cpu cortex-a72 -m "$MEM" "${SMP_ARGS[@]}" >/dev/null 2>&1 || true
        if [ ! -s "$DTB" ]; then
            rm -f "$DTB"
            echo "error: dumpdtb failed" >&2
            exit 1
        fi
        set +e
        "$TOY_VIRT_QEMU" -M virt,gic-version=2 -cpu cortex-a72 -m "$MEM" "${SMP_ARGS[@]}" \
            "${SERIAL_ARGS[@]}" ${DISP_ARGS[@]+"${DISP_ARGS[@]}"} "${DEV_ARGS[@]}" \
            -kernel "$TOY_VIRT_ELF" \
            -device loader,addr=0x4a000000,file="$DTB"
        Ec=$?
        set -e
        rm -f "$DTB"
        exit "$Ec"
    else
        exec "$TOY_VIRT_QEMU" -M virt ${BIOS_ARGS[@]+"${BIOS_ARGS[@]}"} -m "$MEM" "${SMP_ARGS[@]}" \
            "${SERIAL_ARGS[@]}" ${DISP_ARGS[@]+"${DISP_ARGS[@]}"} "${DEV_ARGS[@]}" \
            -kernel "$TOY_VIRT_ELF"
    fi
}

toy_virt_smoke_ok() {
    local Out="$1"
    if ! grep -qE 'user: (back in EL1|back in S-mode|EL0 syscall ok|U-mode syscall ok)' "$Out" 2>/dev/null; then
        return 1
    fi
    if ! grep -qE 'ToyOS ready|ToyOS 就绪' "$Out" 2>/dev/null; then
        return 1
    fi
    if [ "$TOY_VIRT_MODE" = "serial" ]; then
        grep -q 'virt: serial shell' "$Out" 2>/dev/null && return 0
        return 1
    fi
    # headless 桌面：gui + net（N10）+（有盘则挂卷）+ ping 网关
    if ! grep -q '\[mod\] gui' "$Out" 2>/dev/null; then
        return 1
    fi
    if [ "${TOY_VIRT_NONET:-0}" != "1" ]; then
        if ! grep -q '\[mod\] net' "$Out" 2>/dev/null; then
            return 1
        fi
        if ! grep -qE 'boot: virtio-net' "$Out" 2>/dev/null; then
            return 1
        fi
        if ! grep -q 'reply from' "$Out" 2>/dev/null; then
            return 1
        fi
    fi
    if [ "${TOY_VIRT_NODISK:-0}" = "1" ]; then
        return 0
    fi
    if ! grep -qE 'default=TOYOS|TOYOS:|THEME' "$Out" 2>/dev/null; then
        return 1
    fi
    # PR-A13：真 timer IRQ 横幅
    if ! grep -aqE 'timer: (Arm64 CNTV\+GIC|RiscV SBI timer) irq' "$Out" 2>/dev/null; then
        return 1
    fi
    # PR-A14：默认 -smp 2 见 AP hello + idle1；TOY_VIRT_SMP=1 则 single CPU
    if [ "${TOY_VIRT_SMP:-2}" != "1" ]; then
        if ! grep -qE 'smp: hello cpu=' "$Out" 2>/dev/null; then
            return 1
        fi
        if ! grep -qE 'sched: AP entered idle|idle1' "$Out" 2>/dev/null; then
            return 1
        fi
    fi
    # PR-A12：本 arch exec HELLO.ELF
    grep -qE 'Hello Ring3' "$Out" 2>/dev/null
}

toy_virt_run_headless() {
    local Out QPID i
    Out=$(mktemp)
    cleanup() {
        if [ -n "${QPID:-}" ]; then
            kill -9 "$QPID" 2>/dev/null || true
            wait "$QPID" 2>/dev/null || true
        fi
        rm -f "$Out" ${DTB:+"$DTB"}
    }
    trap cleanup EXIT

    toy_virt_qemu_cmd_prefix

    local Cmd=()
    if [ "$TOY_VIRT_ARCH" = "arm64" ]; then
        DTB=$(mktemp)
        "$TOY_VIRT_QEMU" -M virt,gic-version=2,dumpdtb="$DTB" -cpu cortex-a72 -m "$MEM" "${SMP_ARGS[@]}" >/dev/null 2>&1 || true
        if [ ! -s "$DTB" ]; then
            echo "error: dumpdtb failed" >&2
            exit 1
        fi
        Cmd=("$TOY_VIRT_QEMU" -M virt,gic-version=2 -cpu cortex-a72 -m "$MEM" "${SMP_ARGS[@]}"
             "${SERIAL_ARGS[@]}" ${DISP_ARGS[@]+"${DISP_ARGS[@]}"} "${DEV_ARGS[@]}"
             -kernel "$TOY_VIRT_ELF"
             -device loader,addr=0x4a000000,file="$DTB")
    else
        Cmd=("$TOY_VIRT_QEMU" -M virt ${BIOS_ARGS[@]+"${BIOS_ARGS[@]}"} -m "$MEM" "${SMP_ARGS[@]}"
             "${SERIAL_ARGS[@]}" ${DISP_ARGS[@]+"${DISP_ARGS[@]}"} "${DEV_ARGS[@]}"
             -kernel "$TOY_VIRT_ELF")
    fi

    # serial 模式多打一个换行；桌面路径由 ShellTask 吃命令（N10：ping QEMU user 网关）
    local Cmds=$'\nhelp\nvols\nls\ncat THEME.CFG\nmem\nps\n'
    if [ "$TOY_VIRT_MODE" != "serial" ] && [ "${TOY_VIRT_NONET:-0}" != "1" ]; then
        Cmds+=$'ping 10.0.2.2\n'
    fi
    # PR-A12：本 arch HELLO.ELF
    if [ "${TOY_VIRT_NODISK:-0}" != "1" ]; then
        Cmds+=$'exec HELLO.ELF\n'
    fi
    Cmds+=$'halt\n'
    printf '%s' "$Cmds" | "${Cmd[@]}" >"$Out" 2>&1 &
    QPID=$!
    for i in $(seq 1 180); do
        if toy_virt_smoke_ok "$Out"; then
            # 等 halt 或再采一会输出
            sleep 0.4
            cat "$Out"
            exit 0
        fi
        if ! kill -0 "$QPID" 2>/dev/null; then
            break
        fi
        sleep 0.25
    done
    cat "$Out"
    echo "error: timeout waiting for $TOY_VIRT_ARCH virt ($TOY_VIRT_MODE)" >&2
    exit 1
}

toy_virt_main() {
    toy_virt_parse_args "$@"
    toy_virt_resolve_qemu
    toy_virt_ensure_elf
    toy_virt_build_dev_args

    if [ "$TOY_VIRT_MODE" = "gui" ]; then
        toy_virt_run_interactive
    else
        toy_virt_run_headless
    fi
}
