/*
 * Startup.S — QEMU virt aarch64 -kernel 入口（PR-A6）
 */
    .section .text
    .global KernelEntry
    .type KernelEntry, %function
KernelEntry:
    adrp    x0, gBringupStack
    add     x0, x0, :lo12:gBringupStack
    add     sp, x0, #8192
    bl      BringupMain
1:  wfi
    b       1b
    .size KernelEntry, .-KernelEntry

    .section .bss
    .align 4
    .global gBringupStack
gBringupStack:
    .space 8192
    .size gBringupStack, 8192
