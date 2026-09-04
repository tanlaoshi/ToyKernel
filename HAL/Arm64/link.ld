/*
 * link.ld — QEMU virt aarch64（-kernel ELF，无 ToyBoot）
 * RAM 基址 0x40000000（virt 机器约定）
 */
OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)
ENTRY(KernelEntry)

SECTIONS
{
    . = 0x40000000;

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
