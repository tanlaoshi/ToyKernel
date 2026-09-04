OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)
SECTIONS
{
    /* 用户镜像：须低于 SO(0x40080000) 与栈(0x40100000) */
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
}
