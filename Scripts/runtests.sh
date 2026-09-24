#!/bin/sh
# Host 单测。用法：./Scripts/runtests.sh scheduler|memory|fs
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
    make runtests-memory MEMORY=bestfit
    ;;
fs)
    make runtests-fs
    ;;
*)
    echo "usage: runtests.sh scheduler|memory|fs" >&2
    exit 1
    ;;
esac
