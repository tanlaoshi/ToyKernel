/* PR-A12：Arm64 用户 ELF @ HalUserCodeVirt (4GiB) */
ENTRY(_start)
SECTIONS
{
    . = 0x100000000;
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
