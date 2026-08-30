#!/bin/bash
set -e
cd "$(dirname "$0")"

ARCH=${1:-x86_64}
echo "Building ToyKernel for ARCH=$ARCH"

make clean ARCH="$ARCH"
make ARCH="$ARCH"

if [ ! -f Build/Kernel.elf ]; then
    echo "Build failed!"
    exit 1
fi

echo "Build successful: Build/Kernel.elf"
cp Build/Kernel.elf ../ToyImage/

if [ "$ARCH" = "x86_64" ]; then
    cp user/hello.elf ../ToyImage/HELLO.ELF
    cp user/count.elf ../ToyImage/COUNT.ELF
    echo "Copied user/hello.elf -> ../ToyImage/HELLO.ELF"
    echo "Copied user/count.elf -> ../ToyImage/COUNT.ELF"
fi

echo "Copied Build/Kernel.elf -> ../ToyImage/"
ls -lh Build/Kernel.elf
