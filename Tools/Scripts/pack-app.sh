#!/bin/bash
# pack-app.sh — 把已编好的 ELF 填进 Store/packages/<id>/（PR-MOD-app-sample / store-src）
# 用法:
#   ./Tools/Scripts/pack-app.sh <id> <elf-path> [file-name]
# 例:
#   ./Tools/Scripts/pack-app.sh hello Build/User/hello.elf HELLO.ELF
set -e
cd "$(dirname "$0")/../.."
Id="$1"
Elf="$2"
File="${3:-$(basename "$Elf" | tr 'a-z' 'A-Z')}"
if [ -z "$Id" ] || [ -z "$Elf" ] || [ ! -f "$Elf" ]; then
    echo "usage: $0 <id> <elf-path> [FILE.ELF]" >&2
    exit 1
fi
Dest="Store/packages/$Id"
mkdir -p "$Dest"
if [ ! -f "$Dest/PKG.TXT" ]; then
    echo "error: missing $Dest/PKG.TXT — create package metadata first" >&2
    exit 1
fi
cp -f "$Elf" "$Dest/$File"
echo "packed $Elf -> $Dest/$File"
if [ -d ../ToyImage/RootFs/X64/Store/packages ] || [ -d ../ToyImage/RootFs/X64 ]; then
    mkdir -p "../ToyImage/RootFs/X64/Store/packages/$Id"
    cp -f "$Dest/PKG.TXT" "../ToyImage/RootFs/X64/Store/packages/$Id/"
    cp -f "$Dest/$File" "../ToyImage/RootFs/X64/Store/packages/$Id/"
    if [ -d "$Dest/Assets" ]; then
        cp -a "$Dest/Assets" "../ToyImage/RootFs/X64/Store/packages/$Id/"
    fi
    echo "synced -> ToyImage/RootFs/X64/Store/packages/$Id/"
fi
