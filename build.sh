#!/bin/bash

# 清理旧文件
rm -f *.o Kernel.elf

# 编译
gcc -ffreestanding -nostdlib -fno-stack-protector -fno-builtin -c Kernel.c -o Kernel.o
gcc -ffreestanding -nostdlib -fno-stack-protector -fno-builtin -c Video.c -o Video.o
gcc -ffreestanding -nostdlib -fno-stack-protector -fno-builtin -c UI.c -o UI.o

# 链接
ld -T link.ld -e KernelEntry -o Kernel.elf Kernel.o Video.o UI.o

# 检查
if [ -f Kernel.elf ]; then
    echo "Build successful!"
    readelf -h Kernel.elf
else
    echo "Build failed!"
fi

cp ~/tanlaoshi/UEFI/edk2/ToyKernel/Kernel.elf ~/tanlaoshi/UEFI/edk2/ToyImage/