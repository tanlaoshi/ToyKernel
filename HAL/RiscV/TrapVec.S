/*
 * TrapVec.S — PR-A10 缺页 + PR-A11 U-mode ecall
 *
 * HAL_FRAME（HalPort.h）：X[0..31]@0, Vec@256, Err@264, Rip@272, Cs@280,
 *   Rflags@288, Rsp@296, Ss@304；压栈 320 字节。
 *
 * sscratch 约定：U 态 = 内核栈顶；S 态 = 0。
 */
    .section .text
    .align 2
    .global HalTrapVector
    .type HalTrapVector, @function
HalTrapVector:
    csrrw   t0, sscratch, sp
    bnez    t0, 1f
    csrr    sp, sscratch
    csrw    sscratch, zero
    j       2f
1:
    mv      sp, t0
2:
    addi    sp, sp, -320
    sd      zero, 0(sp)
    sd      ra, 8(sp)
    sd      gp, 24(sp)
    sd      tp, 32(sp)
    sd      t0, 40(sp)
    sd      t1, 48(sp)
    sd      t2, 56(sp)
    sd      s0, 64(sp)
    sd      s1, 72(sp)
    sd      a0, 80(sp)
    sd      a1, 88(sp)
    sd      a2, 96(sp)
    sd      a3, 104(sp)
    sd      a4, 112(sp)
    sd      a5, 120(sp)
    sd      a6, 128(sp)
    sd      a7, 136(sp)
    sd      s2, 144(sp)
    sd      s3, 152(sp)
    sd      s4, 160(sp)
    sd      s5, 168(sp)
    sd      s6, 176(sp)
    sd      s7, 184(sp)
    sd      s8, 192(sp)
    sd      s9, 200(sp)
    sd      s10, 208(sp)
    sd      s11, 216(sp)
    sd      t3, 224(sp)
    sd      t4, 232(sp)
    sd      t5, 240(sp)
    sd      t6, 248(sp)

    csrr    t0, sscratch
    sd      t0, 296(sp)
    csrr    t0, sepc
    sd      t0, 272(sp)
    csrr    t0, sstatus
    sd      t0, 288(sp)
    sd      zero, 256(sp)
    sd      zero, 264(sp)
    sd      zero, 280(sp)
    sd      zero, 304(sp)

    mv      a0, sp
    call    HalTrapDispatch

    beqz    a0, 3f
    andi    t0, a0, 1
    bnez    t0, 4f
    andi    a0, a0, -4
    mv      sp, a0
    j       3f
4:
    andi    a0, a0, -4
    ld      t0, 272(a0)
    ld      t1, 296(a0)
    csrw    sscratch, zero
    mv      sp, t1
    jr      t0

3:
    /* 先读 sstatus / Rsp，再恢复 GPR（t0-t2 作临时） */
    ld      t0, 288(sp)
    ld      t1, 296(sp)
    ld      t2, 272(sp)
    csrw    sepc, t2
    csrw    sstatus, t0
    andi    t2, t0, 0x100

    ld      ra, 8(sp)
    ld      gp, 24(sp)
    ld      tp, 32(sp)
    ld      a0, 80(sp)
    ld      a1, 88(sp)
    ld      a2, 96(sp)
    ld      a3, 104(sp)
    ld      a4, 112(sp)
    ld      a5, 120(sp)
    ld      a6, 128(sp)
    ld      a7, 136(sp)
    ld      s0, 64(sp)
    ld      s1, 72(sp)
    ld      s2, 144(sp)
    ld      s3, 152(sp)
    ld      s4, 160(sp)
    ld      s5, 168(sp)
    ld      s6, 176(sp)
    ld      s7, 184(sp)
    ld      s8, 192(sp)
    ld      s9, 200(sp)
    ld      s10, 208(sp)
    ld      s11, 216(sp)
    ld      t3, 224(sp)
    ld      t4, 232(sp)
    ld      t5, 240(sp)
    ld      t6, 248(sp)
    /* t0=sstatus t1=Rsp t2=SPP；牺牲帧内 x5/x6/x7（t0-t2）原值 */

    bnez    t2, 5f
    /* 回 U：t1=user sp；caller-saved t0-t2 不恢复 */
    ld      t0, 40(sp)
    addi    sp, sp, 320
    csrw    sscratch, sp
    mv      sp, t1
    sret
5:
    ld      t0, 40(sp)
    ld      t1, 48(sp)
    ld      t2, 56(sp)
    addi    sp, sp, 320
    csrw    sscratch, zero
    sret

    .global HalUserEnter
    .type HalUserEnter, @function
HalUserEnter:
    /* a0 = HAL_FRAME*；sscratch 已由 SelfTestEnter 设为内核栈 */
    ld      t1, 272(a0)
    csrw    sepc, t1
    ld      t1, 288(a0)
    csrw    sstatus, t1

    ld      ra, 8(a0)
    ld      gp, 24(a0)
    ld      tp, 32(a0)
    ld      t0, 40(a0)
    ld      t1, 48(a0)
    ld      t2, 56(a0)
    ld      s0, 64(a0)
    ld      s1, 72(a0)
    ld      a1, 88(a0)
    ld      a2, 96(a0)
    ld      a3, 104(a0)
    ld      a4, 112(a0)
    ld      a5, 120(a0)
    ld      a6, 128(a0)
    ld      a7, 136(a0)
    ld      s2, 144(a0)
    ld      s3, 152(a0)
    ld      s4, 160(a0)
    ld      s5, 168(a0)
    ld      s6, 176(a0)
    ld      s7, 184(a0)
    ld      s8, 192(a0)
    ld      s9, 200(a0)
    ld      s10, 208(a0)
    ld      s11, 216(a0)
    ld      t3, 224(a0)
    ld      t4, 232(a0)
    ld      t5, 240(a0)
    ld      t6, 248(a0)
    ld      sp, 296(a0)
    ld      a0, 80(a0)
    sret

    .global HalUserSelfTestEnter
    .type HalUserSelfTestEnter, @function
    /* a0=Ksp a1=Frame* */
HalUserSelfTestEnter:
    la      t0, gUserSelfRet
    sd      sp, 0(t0)
    sd      ra, 8(t0)
    sd      s0, 16(t0)
    sd      s1, 24(t0)
    sd      s2, 32(t0)
    sd      s3, 40(t0)
    sd      s4, 48(t0)
    sd      s5, 56(t0)
    sd      s6, 64(t0)
    sd      s7, 72(t0)
    sd      s8, 80(t0)
    sd      s9, 88(t0)
    sd      s10, 96(t0)
    sd      s11, 104(t0)

    mv      sp, a0
    csrw    sscratch, sp
    mv      a0, a1
    j       HalUserEnter

    .global HalUserSelfTestReturn
    .type HalUserSelfTestReturn, @function
HalUserSelfTestReturn:
    la      t0, gUserSelfRet
    ld      sp, 0(t0)
    ld      ra, 8(t0)
    ld      s0, 16(t0)
    ld      s1, 24(t0)
    ld      s2, 32(t0)
    ld      s3, 40(t0)
    ld      s4, 48(t0)
    ld      s5, 56(t0)
    ld      s6, 64(t0)
    ld      s7, 72(t0)
    ld      s8, 80(t0)
    ld      s9, 88(t0)
    ld      s10, 96(t0)
    ld      s11, 104(t0)
    ret

    .size HalTrapVector, .-HalTrapVector

    .bss
    .align 3
    .global gUserSelfRet
gUserSelfRet:
    .space 112
