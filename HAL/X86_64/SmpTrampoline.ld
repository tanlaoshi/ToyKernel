/*
 * SmpTrampoline.ld — AP 启动桩链接到物理 0x8000（再由 BSP 拷贝）
 */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(SmpTrampolineStart)
SECTIONS
{
    . = 0x8000;
    .text : {
        *(.text*)
    }
}
