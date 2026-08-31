/*
 * link.ld — 内核链接脚本
 *
 * 将内核链接到物理/虚拟地址 0x100000；导出 __kernel_end 供 PMM 保留内核镜像。
 */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(KernelEntry)

SECTIONS
{
    . = 0x100000;

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