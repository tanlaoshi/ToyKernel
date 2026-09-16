#!/bin/bash
# PR-A-sdk-pack / PR-A-examples：头文件 / 静库 / 链接脚本 / 示例 → Dist/ToySdk/
# 用法：./Tools/build-sdk.sh [DEST]
set -euo pipefail
cd "$(dirname "$0")/.."

DEST="${1:-Dist/ToySdk}"
SRC_INC=User/include
SRC_LD=User/user.ld
TPL=Tools/Sdk

echo "ToySdk: building CRT / libtoyos / libToy* / libFsUtil"
make ARCH=x86_64 \
	User/crt/crt0.o User/crt/syscall.o \
	User/Library/ToyOs/libtoyos.a \
	User/Library/ToyUi/libToyUi.a \
	User/Library/ToyGfx/libToyGfx.a \
	User/Library/ToyNet/libToyNet.a \
	User/Library/FsUtil/libFsUtil.a

rm -rf "$DEST"
mkdir -p "$DEST/include" "$DEST/Library" "$DEST/Documents" "$DEST/Examples"

cp -a "$SRC_INC"/. "$DEST/include/"
cp -a "$SRC_LD" "$DEST/user.ld"
cp -a User/Library/ToyOs/libtoyos.a \
	User/Library/ToyUi/libToyUi.a \
	User/Library/ToyGfx/libToyGfx.a \
	User/Library/ToyNet/libToyNet.a \
	User/Library/FsUtil/libFsUtil.a \
	User/crt/crt0.o \
	User/crt/syscall.o \
	"$DEST/Library/"

cp -a "$TPL/ToySdk.mk" "$DEST/ToySdk.mk"
cp -a "$TPL/README.md" "$DEST/README.md"
cp -a "$TPL/Examples/." "$DEST/Examples/"
cp -a Documents/应用开发指南.md "$DEST/Documents/"

echo "ToySdk: wrote $DEST"
echo "next: for d in $DEST/Examples/*/; do make -C \"\$d\"; done"
