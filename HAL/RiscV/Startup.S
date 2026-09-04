/*
 * Startup.S — QEMU virt riscv64 入口（PR-V1：a0=hartid a1=DTB；OpenSBI）
 */
    .section .text.boot, "ax"
    .global KernelEntry
    .type KernelEntry, @function
KernelEntry:
    /* OpenSBI / -kernel：a0 = hartid，a1 = DTB */
    mv      s0, a0
    mv      s1, a1

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
    mv      a0, s0
    mv      a1, s1
    call    StartupMain
3:  wfi
    j       3b
    .size KernelEntry, .-KernelEntry
