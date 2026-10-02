/* PR-A12：RiscV 用户 ELF @ HalUserCodeVirt (4GiB) */
OUTPUT_ARCH(riscv)
OUTPUT_FORMAT(elf64-littleriscv)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5); /* PF_R|PF_X */
    data PT_LOAD FLAGS(6); /* PF_R|PF_W */
}

SECTIONS
{
    . = 0x100000000;
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
        *(.sbss*)
    } :data
}
