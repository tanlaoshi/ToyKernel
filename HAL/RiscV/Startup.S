/*
 * Startup.S — QEMU virt riscv64 -kernel 入口（PR-A6）
 */
    .section .text
    .global KernelEntry
    .type KernelEntry, @function
KernelEntry:
    lla     sp, gBringupStack
    li      t0, 8192
    add     sp, sp, t0
    call    BringupMain
1:  wfi
    j       1b
    .size KernelEntry, .-KernelEntry

    .section .bss
    .align 4
    .global gBringupStack
gBringupStack:
    .space 8192
    .size gBringupStack, 8192
