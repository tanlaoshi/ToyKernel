#!/bin/bash
# PR-A-sdk-pack：把用户态头文件 / 静库 / 链接脚本打成可分发的 Dist/ToySdk/
# 用法：./Tools/build-sdk.sh [DEST]
set -euo pipefail
cd "$(dirname "$0")/.."

DEST="${1:-Dist/ToySdk}"
SRC_INC=User/include
SRC_LD=User/user.ld
TPL=Tools/Sdk

echo "ToySdk: building CRT / libtoyos / libToy*"
make ARCH=x86_64 \
	User/crt/crt0.o User/crt/syscall.o \
	User/Library/ToyOs/libtoyos.a \
	User/Library/ToyUi/libToyUi.a \
	User/Library/ToyGfx/libToyGfx.a \
	User/Library/ToyNet/libToyNet.a

rm -rf "$DEST"
mkdir -p "$DEST/include" "$DEST/Library" "$DEST/Documents" "$DEST/Examples/Hello"

cp -a "$SRC_INC"/. "$DEST/include/"
cp -a "$SRC_LD" "$DEST/user.ld"
cp -a User/Library/ToyOs/libtoyos.a \
	User/Library/ToyUi/libToyUi.a \
	User/Library/ToyGfx/libToyGfx.a \
	User/Library/ToyNet/libToyNet.a \
	User/crt/crt0.o \
	User/crt/syscall.o \
	"$DEST/Library/"

cp -a "$TPL/ToySdk.mk" "$DEST/ToySdk.mk"
cp -a "$TPL/README.md" "$DEST/README.md"
cp -a "$TPL/Examples/Hello/Makefile" "$DEST/Examples/Hello/Makefile"
cp -a User/Pkg/main.c "$DEST/Examples/Hello/main.c"
cp -a Documents/应用开发指南.md "$DEST/Documents/"

echo "ToySdk: wrote $DEST"
echo "next: make -C $DEST/Examples/Hello"
