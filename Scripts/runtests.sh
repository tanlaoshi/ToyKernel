#!/bin/sh
# Host 单测。用法：./Scripts/runtests.sh scheduler
set -e
Root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$Root"
case "${1:-}" in
scheduler)
    make runtests
    make runtests SCHEDULER=priority
    ;;
*)
    echo "usage: runtests.sh scheduler" >&2
    exit 1
    ;;
esac
