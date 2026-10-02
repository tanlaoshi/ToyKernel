/*
 * link.ld — 内核链接脚本
 *
 * 将内核链接到物理/虚拟地址 0x100000；导出 __kernel_end 供 PMM 保留内核镜像。
 */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(KernelEntry)

PHDRS
{
    text PT_LOAD FLAGS(5); /* PF_R|PF_X */
    data PT_LOAD FLAGS(6); /* PF_R|PF_W */
}

SECTIONS
{
    . = 0x100000;

    .text : {
        *(.text*)
    } :text

    .rodata : {
        *(.rodata*)
    } :text

    . = ALIGN(0x1000);
    .data : {
        *(.data*)
    } :data

    .bss : {
        *(.bss*)
        *(COMMON)
    } :data

    __kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
