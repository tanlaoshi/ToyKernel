/*
 * Startup.S — QEMU virt riscv64 -kernel 入口
 */
    .section .text.boot, "ax"
    .global KernelEntry
    .type KernelEntry, @function
KernelEntry:
    lla     sp, gBootStack
    li      t0, 65536
    add     sp, sp, t0

    lla     t0, __bss_start
    lla     t1, __bss_end
1:  bgeu    t0, t1, 2f
    sd      zero, 0(t0)
    addi    t0, t0, 8
    j       1b
2:
    call    StartupMain
3:  wfi
    j       3b
    .size KernelEntry, .-KernelEntry
