#!/bin/sh
# test-all.sh — 开课前统一自动化入口（不依赖真机）
#
# 串跑：smoke-boot / smoke-virt / test-shell / test-user / test-fs
#       + runtests scheduler|memory|fs
# 每条 timeout 180s；超时记 FAIL 并继续；最终汇总非零退出。
#
# 用法（在 ToyKernel 根）：./Tools/Scripts/test-all.sh
set -u

Root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
ImageRoot="${TOY_IMAGE_ROOT:-$Root/../ToyImage}"
TimeoutSec="${TOY_TEST_TIMEOUT:-180}"
Fail=0
Ran=0

log() {
    printf '%s\n' "$*"
}

run_one() {
    Name=$1
    shift
    Ran=$((Ran + 1))
    log "==== [$Ran] $Name (timeout ${TimeoutSec}s) ===="
    Ec=0
    if command -v timeout >/dev/null 2>&1; then
        timeout "$TimeoutSec" "$@"
        Ec=$?
    else
        "$@"
        Ec=$?
    fi
    if [ "$Ec" -eq 0 ]; then
        log "PASS: $Name"
        return 0
    fi
    if [ "$Ec" -eq 124 ]; then
        log "FAIL: $Name (timeout ${TimeoutSec}s)"
    else
        log "FAIL: $Name (exit $Ec)"
    fi
    Fail=$((Fail + 1))
    return 0
}

if [ ! -d "$ImageRoot/Scripts" ]; then
    log "error: ToyImage Scripts not found: $ImageRoot/Scripts"
    log "hint: set TOY_IMAGE_ROOT=.../ToyImage or place ToyImage next to ToyKernel"
    exit 2
fi

cd "$Root" || exit 2

run_one "runtests scheduler" ./Tools/Scripts/runtests.sh scheduler
run_one "runtests memory" ./Tools/Scripts/runtests.sh memory
run_one "runtests fs" ./Tools/Scripts/runtests.sh fs

cd "$ImageRoot" || exit 2
run_one "smoke-boot" ./Scripts/smoke-boot.sh
run_one "smoke-virt" ./Scripts/smoke-virt.sh
# expect 脚本用 dirname(pwd) 推 Image 根 → 必须在 Scripts/ 下跑
cd "$ImageRoot/Scripts" || exit 2
run_one "test-shell" ./test-shell.sh
run_one "test-user" ./test-user.sh
run_one "test-fs" ./test-fs.sh
run_one "test-enosys" ./test-enosys.sh

log "==== summary: ran=$Ran fail=$Fail ===="
if [ "$Fail" -ne 0 ]; then
    exit 1
fi
exit 0
