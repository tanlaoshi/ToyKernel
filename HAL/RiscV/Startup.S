/*
 * Startup.S — QEMU virt riscv64 入口（PR-V1/A14：a0=hartid a1=DTB；OpenSBI）
 * A14：OpenSBI Boot HART 常为非 0（如 -smp 2 时 hart 1）。用原子抢 BSP，
 * 勿用 hartid==0；其余 hart SecondaryPark，等 soft-release / 或由 HSM 进 HalApEntry。
 */
    .section .data
    .align 3
    .global gBspClaimed
gBspClaimed:
    .word 0
    .global gBspHartId
gBspHartId:
    .dword 0

    .section .text.boot, "ax"
    .global KernelEntry
    .type KernelEntry, @function
KernelEntry:
    /* OpenSBI / -kernel：a0 = hartid，a1 = DTB */
    mv      s0, a0
    mv      s1, a1

    /* 首个进入的 hart 为 BSP（gBspClaimed 在 .data，BSS 清零前可读） */
    lla     t0, gBspClaimed
    li      t1, 1
    amoswap.w.aq t2, t1, (t0)
    bnez    t2, SecondaryPark

    lla     t0, gBspHartId
    sd      s0, 0(t0)

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

/*
 * 次 hart（与 BSP 同进 payload）：等 gApGo[hartid]，再按 gApLogical 取栈进 HalApMain。
 * 勿清 BSS、勿跑 StartupMain。HSM 拉起的 hart 走 HalApEntry，不经此处。
 */
SecondaryPark:
    lla     t0, gApGo
    slli    t1, s0, 2
    add     t0, t0, t1
    li      t2, 0x41313447          /* SMP_GO_MAGIC 'A14G' */
4:  lw      t1, 0(t0)
    beq     t1, t2, 5f
    wfi
    j       4b
5:
    lla     t0, gApLogical
    slli    t1, s0, 2
    add     t0, t0, t1
    lw      s1, 0(t0)              /* logical CPU */

    lla     t0, gApStackTop
    slli    t1, s1, 3
    add     t0, t0, t1
    ld      sp, 0(t0)
    mv      a0, s1
    call    HalApMain
6:  wfi
    j       6b
    .size KernelEntry, .-KernelEntry
