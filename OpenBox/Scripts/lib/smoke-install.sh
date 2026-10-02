#!/bin/bash
# PR-FS-inst-1：AHCI + 空白第三盘；Guest install 2 --yes --mib 512
#
#   ./smoke-install.sh
# 约 1 分钟；进度在终端；成功后自动结束（不必 Ctrl+C）
set -eu
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=toyos-common.sh
. "$SCRIPT_DIR/toyos-common.sh"
IMAGE_ROOT="$(toyos_image_root "$0")"
cd "$IMAGE_ROOT"

cleanup() {
  pkill -9 -f 'qemu-system-x86_64' 2>/dev/null || true
}
trap cleanup EXIT

echo "smoke-install: kill leftover QEMU..."
cleanup
sleep 0.5
rm -f /tmp/toyos-install-target.img

export TOY_KILL_QEMU=1 TOY_HEADLESS=1 TOY_SMP=1 TOY_NO_HOSTFWD=1
export TOY_DISK=ahci TOY_INSTALL_DISK=1 TOY_INSTALL_MIB=512

LOG=/tmp/toy-fsinst-guest.log
rm -f "$LOG"
: >"$LOG"

echo "smoke-install: QEMU ahci + blank disk → drive 2"
echo "smoke-install: log=$LOG"

# 管道放后台：否则会卡在 QEMU 不退出（stdin EOF 后仍跑）
(
  ready=0
  for i in $(seq 1 120); do
    if tr -d '\r' <"$LOG" 2>/dev/null | grep -aq 'ToyOS ready\|ToyOS 就绪'; then
      ready=1
      break
    fi
    if [ $((i % 4)) -eq 0 ]; then
      echo "smoke-install: waiting ready... ${i}/120" >&2
    fi
    sleep 0.5
  done
  if [ "$ready" != 1 ]; then
    echo "smoke-install: timeout (no ToyOS ready)" >&2
    exit 0
  fi
  echo "smoke-install: ready — sending install 2 --yes --mib 512" >&2
  sleep 1
  printf '\r'
  sleep 1
  printf 'install disks\r'
  sleep 2
  printf 'install 2 --yes --mib 512\r'
  # 等结果，最多 ~70s；看到 ok 就结束喂键
  for i in $(seq 1 70); do
    if tr -d '\r' <"$LOG" 2>/dev/null | grep -aq 'install: ok'; then
      echo "smoke-install: saw install: ok" >&2
      printf 'vols\r'
      sleep 2
      exit 0
    fi
    if tr -d '\r' <"$LOG" 2>/dev/null | grep -aq 'install: failed\|install: GPT fail'; then
      echo "smoke-install: saw install failure" >&2
      exit 0
    fi
    sleep 1
  done
  echo "smoke-install: timeout waiting install: ok" >&2
) | "$SCRIPT_DIR/run-split.sh" --kill-qemu --headless --smp=1 >>"$LOG" 2>&1 &
PIPE_PID=$!

# 父进程：等到 ok / 失败 / 超时，再杀 QEMU（关键：不要干等 QEMU 自己退出）
ok=0
for i in $(seq 1 150); do
  if tr -d '\r' <"$LOG" 2>/dev/null | grep -aq 'install: ok'; then
    ok=1
    break
  fi
  if tr -d '\r' <"$LOG" 2>/dev/null | grep -aq 'install: failed\|install: GPT fail'; then
    break
  fi
  if ! kill -0 "$PIPE_PID" 2>/dev/null; then
    break
  fi
  sleep 1
done

cleanup
wait "$PIPE_PID" 2>/dev/null || true
sleep 0.2

echo '=== guest (filtered) ==='
tr -d '\r' <"$LOG" | grep -aE 'install:|ahci drives|ToyOS ready|drive |EXCEPTION|hosts volume|empty' | tail -60 || true

if [ "$ok" = 1 ] || tr -d '\r' <"$LOG" | grep -aq 'install: ok'; then
  echo 'FS-INST: PASS'
  exit 0
fi
echo 'FS-INST: FAIL — full log: '"$LOG"
echo 'Manual interactive:'
echo '  TOY_DISK=ahci TOY_INSTALL_DISK=1 "$SCRIPT_DIR/run-split.sh"'
echo '  toyos> install disks'
echo '  toyos> install 2 --yes --mib 512'
exit 1
