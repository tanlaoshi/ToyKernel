/* PR-A12：RiscV 用户 ELF @ HalUserCodeVirt (4GiB) */
OUTPUT_ARCH(riscv)
OUTPUT_FORMAT(elf64-littleriscv)
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
