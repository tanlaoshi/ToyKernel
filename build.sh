#!/bin/bash

rm -f *.o Kernel.elf

gcc -ffreestanding -nostdlib -fno-stack-protector -fno-builtin -c Kernel.c -o Kernel.o
gcc -ffreestanding -nostdlib -fno-stack-protector -fno-builtin -c Video.c -o Video.o
gcc -ffreestanding -nostdlib -fno-stack-protector -fno-builtin -c UI.c -o UI.o
gcc -ffreestanding -nostdlib -fno-stack-protector -fno-builtin -c PCIe.c -o PCIe.o

ld -T link.ld -e KernelEntry -o Kernel.elf Kernel.o Video.o UI.o PCIe.o

if [ -f Kernel.elf ]; then
    echo "Build successful!"
    ls -lh Kernel.elf
else
    echo "Build failed!"
    exit 1
fi

cp ~/tanlaoshi/edk2/ToyKernel/Kernel.elf ~/tanlaoshi/edk2/ToyImage/