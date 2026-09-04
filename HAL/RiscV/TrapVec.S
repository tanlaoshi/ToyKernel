/*
 * Trap.S — PR-A10：S-mode 陷阱入口（缺页 → C）
 */
    .section .text
    .align 2
    .global HalTrapVector
    .type HalTrapVector, @function
HalTrapVector:
    addi    sp, sp, -256
    sd      ra, 0(sp)
    sd      t0, 8(sp)
    sd      t1, 16(sp)
    sd      t2, 24(sp)
    sd      a0, 32(sp)
    sd      a1, 40(sp)
    sd      a2, 48(sp)
    sd      a3, 56(sp)
    sd      a4, 64(sp)
    sd      a5, 72(sp)
    sd      a6, 80(sp)
    sd      a7, 88(sp)
    sd      t3, 96(sp)
    sd      t4, 104(sp)
    sd      t5, 112(sp)
    sd      t6, 120(sp)

    csrr    a0, scause
    csrr    a1, stval
    csrr    a2, sepc
    call    HalTrapSync
    beqz    a0, 1f
    csrw    sepc, a0
1:
    ld      t6, 120(sp)
    ld      t5, 112(sp)
    ld      t4, 104(sp)
    ld      t3, 96(sp)
    ld      a7, 88(sp)
    ld      a6, 80(sp)
    ld      a5, 72(sp)
    ld      a4, 64(sp)
    ld      a3, 56(sp)
    ld      a2, 48(sp)
    ld      a1, 40(sp)
    ld      a0, 32(sp)
    ld      t2, 24(sp)
    ld      t1, 16(sp)
    ld      t0, 8(sp)
    ld      ra, 0(sp)
    addi    sp, sp, 256
    sret
    .size HalTrapVector, .-HalTrapVector
