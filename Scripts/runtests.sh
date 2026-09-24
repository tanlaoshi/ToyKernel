#!/bin/sh
# Host 单测。用法：./Scripts/runtests.sh scheduler|memory
set -e
Root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$Root"
case "${1:-}" in
scheduler)
    make runtests
    make runtests SCHEDULER=priority
    ;;
memory)
    make runtests-memory
    ;;
*)
    echo "usage: runtests.sh scheduler|memory" >&2
    exit 1
    ;;
esac
