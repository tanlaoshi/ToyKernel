#!/bin/bash
# PR-BOOT-fast-0：无头开机计时（不改内核；只抓串口墙钟）
# 用法：
#   ./Scripts/measure-boot.sh              # 跑 1 次
#   ./Scripts/measure-boot.sh 3            # 跑 3 次
#   MEASURE_DIR=/tmp/foo ./Scripts/measure-boot.sh 3
#
# 锚点（相对本脚本启动 qemu 的 t0）：
#   [Mod] Gui        — DesktopInit 含在 Gui 内
#   [Mod] Scheduler  — Gui 返回后；QEMU 上作「桌面可点」代理
#   ToyOS ready      — 串口就绪行
# 真机「桌面可点」另记墙钟（见 ToyKernel Documents/开发/开机流程与加速.md §7）。
set -eu
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=toyos-common.sh
. "$SCRIPT_DIR/toyos-common.sh"
IMAGE_ROOT="$(toyos_image_root "$0")"
cd "$IMAGE_ROOT"

RUNS="${1:-1}"
TIMEOUT_SEC="${MEASURE_TIMEOUT:-90}"
STAMP_DIR="${MEASURE_DIR:-/tmp/toyos-boot-measure}"
mkdir -p "$STAMP_DIR"

if [ ! -f RootFs/X64/Kernel.elf ]; then
    echo "error: missing RootFs/X64/Kernel.elf — build ToyKernel first" >&2
    exit 1
fi

run_once() {
    local n="$1"
    local stamped="${STAMP_DIR}/run${n}.ts.log"
    export TOY_KILL_QEMU=1
    export TOY_HEADLESS=1
    export TOY_NO_HOSTFWD=1
    export TOY_SMP="${TOY_SMP:-1}"

    MEASURE_N="$n" MEASURE_LOG="$stamped" MEASURE_TIMEOUT="$TIMEOUT_SEC" \
    MEASURE_RUN="$SCRIPT_DIR/run-split.sh" \
    python3 - <<'PY'
import os, re, signal, subprocess, sys, time

n = os.environ["MEASURE_N"]
log_path = os.environ["MEASURE_LOG"]
timeout = int(os.environ.get("MEASURE_TIMEOUT", "90"))
run_split = os.environ["MEASURE_RUN"]
smp = os.environ.get("TOY_SMP", "1")

def kill_qemu():
    subprocess.run(
        ["pkill", "-9", "-f", "qemu-system-x86_64"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )

kill_qemu()
time.sleep(0.2)

t0 = time.time()
cmd = [run_split, "--kill-qemu", "--headless", f"--smp={smp}"]
proc = subprocess.Popen(
    cmd,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    text=True,
    bufsize=1,
)

ready_re = re.compile(r"ToyOS ready|ToyOS 就绪")
ok = False
try:
    with open(log_path, "w", encoding="utf-8", errors="replace") as out:
        assert proc.stdout is not None
        while True:
            if time.time() - t0 > timeout:
                break
            line = proc.stdout.readline()
            if line == "" and proc.poll() is not None:
                break
            if line == "":
                time.sleep(0.05)
                continue
            now = time.time()
            text = line.rstrip("\n").replace("\r", "")
            stamp = "[%s +%.3fs] %s\n" % (
                time.strftime("%H:%M:%S", time.localtime(now)),
                now - t0,
                text,
            )
            out.write(stamp)
            out.flush()
            if ready_re.search(text):
                ok = True
                # 再吸几行缓冲后停
                time.sleep(0.15)
                break
finally:
    if proc.poll() is None:
        try:
            proc.send_signal(signal.SIGTERM)
            proc.wait(timeout=2)
        except Exception:
            proc.kill()
    kill_qemu()

def rel(pat: str):
    rx = re.compile(pat)
    with open(log_path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if rx.search(line):
                m = re.search(r"\+([0-9.]+)s\]", line)
                return m.group(1) if m else "-"
    return "-"

if not ok:
    print(f"measure: FAIL run#{n} — no ToyOS ready (log={log_path})", file=sys.stderr)
    sys.exit(1)

keys = [
    ("serial", r"\[Mod\] Serial"),
    ("usb", r"\[Mod\] Usb"),
    ("fs", r"\[Mod\] FileSystem"),
    ("gui", r"\[Mod\] Gui"),
    ("sched", r"\[Mod\] Scheduler"),
    ("console", r"\[Mod\] Console"),
    ("ready", r"ToyOS ready|ToyOS 就绪"),
]
vals = {k: rel(p) for k, p in keys}
print(f"measure: run#{n} PASS")
print(f"  log              {log_path}")
print(f"  [Mod] Serial     {vals['serial']}s")
print(f"  [Mod] Usb        {vals['usb']}s")
print(f"  [Mod] FileSystem {vals['fs']}s")
print(f"  [Mod] Gui        {vals['gui']}s")
print(f"  [Mod] Scheduler  {vals['sched']}s  ← QEMU「桌面可点」代理")
print(f"  [Mod] Console    {vals['console']}s")
print(f"  ToyOS ready      {vals['ready']}s")
print(
    "CSV run=%s serial=%s usb=%s fs=%s gui=%s sched=%s console=%s ready=%s"
    % (
        n,
        vals["serial"],
        vals["usb"],
        vals["fs"],
        vals["gui"],
        vals["sched"],
        vals["console"],
        vals["ready"],
    )
)
sys.exit(0)
PY
}

echo "measure: runs=${RUNS} TOY_SMP=${TOY_SMP:-1} timeout=${TIMEOUT_SEC}s dir=${STAMP_DIR}"
fail=0
r=1
while [ "$r" -le "$RUNS" ]; do
    if ! run_once "$r"; then
        fail=1
    fi
    sleep 0.3
    r=$((r + 1))
done
exit "$fail"
