OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5); /* PF_R|PF_X */
    data PT_LOAD FLAGS(6); /* PF_R|PF_W */
}

SECTIONS
{
    /* 用户镜像：须低于 SO(0x40080000) 与栈(0x40100000) */
    . = 0x40000000;
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
}
