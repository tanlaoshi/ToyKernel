/*
 * link.ld — QEMU virt riscv64（OpenSBI payload @ 0x80200000，PR-V1）
 */
OUTPUT_FORMAT(elf64-littleriscv)
OUTPUT_ARCH(riscv)
ENTRY(KernelEntry)

SECTIONS
{
    . = 0x80200000;

    .text : {
        KEEP(*(.text.boot))
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
        *(.sdata*)
    }

    .bss : {
        __bss_start = .;
        *(.sbss*)
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }

    .bootstack (NOLOAD) : {
        . = ALIGN(16);
        gBootStack = .;
        . = . + 65536;
    }

    __kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
