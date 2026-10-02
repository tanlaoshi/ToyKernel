#!/usr/bin/env bash
# ToyOS 坏味道静态检查（P0）
# 用法：bash Scripts/check-smells.sh [--strict]
# 退出码：0 = 无违规或非严格；1 = 有违规且 --strict；2 = 环境缺依赖
set -u

STRICT=0
[ "${1:-}" = "--strict" ] && STRICT=1

HERE="$(cd "$(dirname "$0")" && pwd)"
# ToyOS/Scripts → $TOYOS_ROOT/ToyKernel；OpenBox/Scripts → ToyKernel；仓根 Scripts 旁挂 Common
if [ -d "$HERE/../ToyKernel/Common" ]; then
    KER="$(cd "$HERE/../ToyKernel" && pwd)"
elif [ -d "$HERE/../../Common" ]; then
    KER="$(cd "$HERE/../.." && pwd)"
elif [ -d "$HERE/../Common" ]; then
    KER="$(cd "$HERE/.." && pwd)"
else
    echo "check-smells: 找不到 ToyKernel 源码根（Common/）" >&2
    exit 2
fi
cd "$KER"

command -v rg >/dev/null 2>&1 || {
    echo "check-smells: 需要 ripgrep(rg)" >&2
    exit 2
}

GLOBS=(
    --glob '!Documents/**'
    --glob '!Build/**'
    --glob '!ThirdParty/**'
    --glob '!Tools/Extract/**'
    --glob '!Tools/Tarballs/**'
    --glob '!Tools/Debs/**'
    --glob '!Tools/Root/**'
)

FAIL=0
report() {
    echo "❌ $1"
    FAIL=1
}
ok() {
    echo "✅ $1"
}

echo "== P0 坏味道检查 =="
echo "ROOT=$KER"

# 1.1 跨层依赖
if rg -q "${GLOBS[@]}" '#include[[:space:]]+"HAL/.*/Drivers/' Common/ Include/ User/ 2>/dev/null; then
    report "1.1 Common/Include/User 不得 include HAL/**/Drivers/*"
    rg -n "${GLOBS[@]}" '#include[[:space:]]+"HAL/.*/Drivers/' Common/ Include/ User/ || true
else
    ok "1.1 无 HAL/Drivers 跨层 include"
fi

if rg -q "${GLOBS[@]}" '#include[[:space:]]+"(Common|HAL|Include)/' User/ 2>/dev/null; then
    report "1.1b User 不得 include 内核头 (Common/HAL/Include)"
    rg -n "${GLOBS[@]}" '#include[[:space:]]+"(Common|HAL|Include)/' User/ || true
else
    ok "1.1b User 未 include 内核头"
fi

if rg -q "${GLOBS[@]}" '#include[[:space:]]+"Console.h"' Common/Library/ 2>/dev/null; then
    report "1.1c Library 不得 include Console.h"
    rg -n "${GLOBS[@]}" '#include[[:space:]]+"Console.h"' Common/Library/ || true
else
    ok "1.1c Library 未 include Console.h"
fi

if rg -q "${GLOBS[@]}" '#ifdef[[:space:]]+(BOARD_|SOC_)' Common/ Include/ User/ 2>/dev/null; then
    report "1.1d Common/Include/User 不得有 BOARD_/SOC_ ifdef"
    rg -n "${GLOBS[@]}" '#ifdef[[:space:]]+(BOARD_|SOC_)' Common/ Include/ User/ || true
else
    ok "1.1d 无板级 ifdef 进 Common"
fi

# 1.2 魔法数字
MAGIC_GLOBS=(
    --glob '*.c'
    --glob '!**/Fonts/**'
    --glob '!**/FontData*'
    --glob '!**/PciNames*'
    --glob '!**/*Test*'
    "${GLOBS[@]}"
)
MAGIC=$(rg -n "${MAGIC_GLOBS[@]}" '\b(0x[0-9A-Fa-f]{4,}|[0-9]{3,})\b' Core/ Common/ HAL/ 2>/dev/null | head -50 || true)
if [ -n "$MAGIC" ]; then
    report "1.2 发现疑似魔法数字（前 50 条）"
    echo "$MAGIC"
else
    ok "1.2 未发现明显魔法数字"
fi

# 1.3 忽略返回值（启发式；需人工确认）
RET_GLOBS=(
    --glob '*.c'
    "${GLOBS[@]}"
)
RET=$(rg -n "${RET_GLOBS[@]}" \
    '^[[:space:]]*(Fat|Block|FileSystem|Vfs|Process|Scheduler|VirtualMemory|PhysicalMemory|VfsService|Store)[[:alnum:]_]*[[:space:]]*\(' \
    Core/ Common/ HAL/ 2>/dev/null \
    | rg -v '\(void\)' \
    | rg -v '=' \
    | rg -v '^[[:space:]]*//' \
    | head -50 || true)
if [ -n "$RET" ]; then
    report "1.3 疑似忽略返回值（前 50 条；需人工确认）"
    echo "$RET"
else
    ok "1.3 未发现明显忽略返回值"
fi

echo "== 汇总 =="
if [ "$FAIL" -eq 0 ]; then
    echo "PASS：无 P0 违规"
    exit 0
fi
if [ "$STRICT" -eq 1 ]; then
    echo "FAIL：存在 P0 违规（--strict）"
    exit 1
fi
echo "WARN：存在 P0 违规（非 strict，不阻塞）"
exit 0
