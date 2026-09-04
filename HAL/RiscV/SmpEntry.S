/*
 * SmpEntry.S — PR-A14：SBI HSM hart_start 入口（a0=hartid a1=opaque=logical）
 */
    .section .text, "ax"
    .globl HalApEntry
    .type HalApEntry, @function
HalApEntry:
    /* a1 = logical CPU（BSP 传入的 opaque） */
    mv      s0, a1

    lla     t0, gApStackTop
    slli    t1, s0, 3
    add     t0, t0, t1
    ld      sp, 0(t0)

    mv      a0, s0
    call    HalApMain
1:  wfi
    j       1b
    .size HalApEntry, .-HalApEntry
