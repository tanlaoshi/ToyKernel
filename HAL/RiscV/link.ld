/*
 * link.ld — QEMU virt riscv64（-kernel ELF，无 ToyBoot）
 * RAM 基址 0x80000000（virt 机器约定）
 */
OUTPUT_FORMAT(elf64-littleriscv)
OUTPUT_ARCH(riscv)
ENTRY(KernelEntry)

SECTIONS
{
    . = 0x80000000;

    .text : {
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        *(.bss*)
        *(COMMON)
    }

    __kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
