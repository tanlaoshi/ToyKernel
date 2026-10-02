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
#   TOY_VIRT_ELF=$TOYOS_ROOT/Build/ToyKernel/HAL/<Arch>/Kernel.elf（可省略，由本脚本填）
#   TOY_VIRT_MAKE_ARCH=arm64|riscv
#   TOY_VIRT_HAL_ARCH=Arm64|RiscV
#   可选：TOY_VIRT_QEMU_EXTRA=(...)  TOY_VIRT_HELLO_PAT=...
#
# 用法（经包装脚本）：
#   ./Scripts/run-virt-arm.sh              # 默认：窗口 + 盘 + 输入（交互）
#   ./Scripts/run-virt-arm.sh --headless   # CI：-nographic 串口冒烟后退出
#   ./Scripts/run-virt-arm.sh --serial     # 无 ramfb 的 A8 串口子集冒烟
#   ./Scripts/run-virt-arm.sh --help

# 本脚本位于 Scripts/lib/；镜像在 ToyImage/，内核旁仓 ToyKernel/
toy_virt_paths() {
    local Here
    Here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    # Scripts/lib → 树根 → ToyImage
    if [ -z "${TOY_IMAGE:-}" ]; then
        if [ -d "$Here/../../ToyImage" ]; then
            TOY_IMAGE="$(cd "$Here/../../ToyImage" && pwd)"
        else
            TOY_IMAGE="$(cd "$Here/.." && pwd)"
        fi
    fi
    if [ -z "${TOY_KERNEL:-}" ]; then
        TOY_KERNEL="$(cd "$TOY_IMAGE/../ToyKernel" && pwd)"
    fi
    if [ -z "${TOYOS_ROOT:-}" ]; then
        TOYOS_ROOT="$(cd "$TOY_IMAGE/.." && pwd)"
    fi
    if type toyos_kernel_build_dir >/dev/null 2>&1; then
        TOY_KERNEL_BUILD="$(toyos_kernel_build_dir "$TOYOS_ROOT" "$TOY_KERNEL")"
    else
        TOY_KERNEL_BUILD="$TOYOS_ROOT/Build/ToyKernel"
        mkdir -p "$TOY_KERNEL_BUILD"
    fi
    export TOY_IMAGE TOY_KERNEL TOYOS_ROOT TOY_KERNEL_BUILD
}

toy_virt_usage() {
    cat <<'EOF'
ToyOS virt 验收（自有 Boot；脚本在 ToyImage，内核在 ../ToyKernel）

  ./Scripts/run-virt-arm.sh | ./Scripts/run-virt-riscv.sh [选项] [Kernel.elf]

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

注意：本路径始终 BOARD=virt（忽略环境 BOARD=）；真机板包请用 ./build.sh … BOARD=<name>。

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
    toy_virt_paths
    if ! command -v "$TOY_VIRT_QEMU" >/dev/null 2>&1; then
        if [ -x "$TOY_KERNEL/Tools/Root/usr/bin/$TOY_VIRT_QEMU" ]; then
            TOY_VIRT_QEMU="$TOY_KERNEL/Tools/Root/usr/bin/$TOY_VIRT_QEMU"
        else
            echo "error: $TOY_VIRT_QEMU not found" >&2
            exit 1
        fi
    fi
}

toy_virt_ensure_elf() {
    local WantBoard Stamp PrevBoard NeedBuild HelloElf
    toy_virt_paths
    cd "$TOY_KERNEL"
    # QEMU virt 验收必须用板包 virt；忽略环境里残留的 BOARD=milk-v-duo-s 等
    WantBoard=virt
    BOARD=virt
    if [ -n "$TOY_VIRT_ELF_ARG" ]; then
        TOY_VIRT_ELF="$TOY_VIRT_ELF_ARG"
    fi
    TOY_VIRT_ELF="${TOY_VIRT_ELF:-$TOY_KERNEL_BUILD/HAL/${TOY_VIRT_HAL_ARCH}/Kernel.elf}"
    # 相对路径：旧 Build/… → 树根产物；其它相对 ToyKernel
    case "$TOY_VIRT_ELF" in
        /*) ;;
        Build/*) TOY_VIRT_ELF="$TOY_KERNEL_BUILD/${TOY_VIRT_ELF#Build/}" ;;
        *) TOY_VIRT_ELF="$TOY_KERNEL/$TOY_VIRT_ELF" ;;
    esac
    Stamp="$TOY_KERNEL_BUILD/HAL/${TOY_VIRT_HAL_ARCH}/.toy_board"
    PrevBoard=""
    if [ -f "$Stamp" ]; then
        PrevBoard=$(tr -d '\n' <"$Stamp" 2>/dev/null || true)
    fi
    NeedBuild=0
    if [ ! -f "$TOY_VIRT_ELF" ]; then
        NeedBuild=1
    elif [ "$PrevBoard" != "$WantBoard" ]; then
        # 缺 stamp 或 stamp≠virt：前一次可能是 duo-s 等真机板包，不能直接 -kernel 上 virt
        NeedBuild=1
        echo "note: $TOY_VIRT_ELF board='${PrevBoard:-unknown}' → rebuilding BOARD=$WantBoard"
        make clean "ARCH=$TOY_VIRT_MAKE_ARCH" "BOARD=$WantBoard" "BUILDDIR=$TOY_KERNEL_BUILD"
    fi
    # Arm64/RiscV 尚无 HAL/<Arch>/LwIp 端口；缺目录时勿用默认 LWIP=1（会找 lwipopts.h 失败）
    VirtLwip=1
    if [ ! -d "HAL/${TOY_VIRT_HAL_ARCH}/LwIp" ]; then
        VirtLwip=0
    fi
    if [ "$NeedBuild" = "1" ]; then
        echo "building ARCH=$TOY_VIRT_MAKE_ARCH BOARD=$WantBoard LWIP=$VirtLwip ..."
        make "ARCH=$TOY_VIRT_MAKE_ARCH" "BOARD=$WantBoard" BRINGUP=0 "LWIP=$VirtLwip" "BUILDDIR=$TOY_KERNEL_BUILD"
    fi
    if [ ! -f "$TOY_VIRT_ELF" ]; then
        echo "error: missing $TOY_VIRT_ELF" >&2
        exit 1
    fi
    # PR-A12：确保本 arch HELLO.ELF 已构建（prepare 会装入盘）
    HelloElf="$TOY_KERNEL_BUILD/HAL/${TOY_VIRT_HAL_ARCH}/user/hello.elf"
    if [ ! -f "$HelloElf" ]; then
        make "ARCH=$TOY_VIRT_MAKE_ARCH" "BOARD=$WantBoard" BRINGUP=0 "LWIP=$VirtLwip" "BUILDDIR=$TOY_KERNEL_BUILD" "$HelloElf"
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
        export TOY_VIRT_MAKE_ARCH TOY_VIRT_HAL_ARCH
        toy_virt_paths
        "$TOY_IMAGE/Scripts/prepare-virt-rootfs.sh" >/dev/null
        # N10：用 raw FAT 镜像，避免 QEMU fat:rw(vvfat) 与 virtio-net 同机 TX 故障
        DEV_ARGS+=(-drive "if=none,id=toyroot,format=raw,file=$TOY_IMAGE/RootFs/${TOY_VIRT_HAL_ARCH}/disk.img"
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
    # BootLog 现行为 `[Mod] Gui` / `Boot: VirtIO-Net` 等 Title Case；匹配一律 -i
    if ! grep -qiE 'user: (back in EL1|back in S-mode|EL0 syscall ok|U-mode syscall ok)' "$Out" 2>/dev/null; then
        return 1
    fi
    if ! grep -qiE 'ToyOS ready|ToyOS 就绪' "$Out" 2>/dev/null; then
        return 1
    fi
    if [ "$TOY_VIRT_MODE" = "serial" ]; then
        grep -qi 'virt: serial shell' "$Out" 2>/dev/null && return 0
        return 1
    fi
    # headless 桌面：gui + net（N10）+（有盘则挂卷）+ ping 网关
    if ! grep -qiE '\[mod\] gui' "$Out" 2>/dev/null; then
        return 1
    fi
    if [ "${TOY_VIRT_NONET:-0}" != "1" ]; then
        if ! grep -qiE '\[mod\] net' "$Out" 2>/dev/null; then
            return 1
        fi
        if ! grep -qiE 'boot: virtio-net' "$Out" 2>/dev/null; then
            return 1
        fi
        if ! grep -qi 'reply from' "$Out" 2>/dev/null; then
            return 1
        fi
    fi
    if [ "${TOY_VIRT_NODISK:-0}" = "1" ]; then
        return 0
    fi
    if ! grep -qiE 'default=TOYOS|TOYOS:|THEME' "$Out" 2>/dev/null; then
        return 1
    fi
    # PR-A13：真 timer IRQ 横幅
    if ! grep -aiqE 'timer: (Arm64 CNTV\+GIC|RiscV SBI timer) irq' "$Out" 2>/dev/null; then
        return 1
    fi
    # PR-A14：默认 -smp 2 见 AP hello + idle1；TOY_VIRT_SMP=1 则 single CPU
    if [ "${TOY_VIRT_SMP:-2}" != "1" ]; then
        if ! grep -qiE 'smp: hello cpu=' "$Out" 2>/dev/null; then
            return 1
        fi
        if ! grep -qiE 'sched: AP entered idle|idle1' "$Out" 2>/dev/null; then
            return 1
        fi
    fi
    # PR-A12：本 arch exec HELLO.ELF
    grep -qiE 'Hello Ring3' "$Out" 2>/dev/null
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
