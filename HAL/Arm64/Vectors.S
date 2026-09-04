/*
 * Vectors.S — PR-A10 缺页 + PR-A11 Lower-EL SVC / 用户帧
 * VBAR_EL1 要求 2KiB 对齐；每槽 0x80 字节。
 *
 * HAL_FRAME（与 HalPort.h 一致）：
 *   X[0..30] @0, Vec@248, Err@256, Rip@264, Cs@272, Rflags@280, Rsp@288, Ss@296
 */
    .section .text
    .align 11
    .global HalExceptionVectors
    .type HalExceptionVectors, %function
HalExceptionVectors:
    /* Current EL with SP_EL0 */
    .org HalExceptionVectors + 0x000
    b       HalExcUnexpected
    .org HalExceptionVectors + 0x080
    b       HalExcUnexpected
    .org HalExceptionVectors + 0x100
    b       HalExcUnexpected
    .org HalExceptionVectors + 0x180
    b       HalExcUnexpected

    /* Current EL with SP_ELx（内核缺页 / IRQ） */
    .org HalExceptionVectors + 0x200
    b       HalExcSync
    .org HalExceptionVectors + 0x280
    b       HalExcIrq
    .org HalExceptionVectors + 0x300
    b       HalExcUnexpected
    .org HalExceptionVectors + 0x380
    b       HalExcUnexpected

    /* Lower EL AArch64（用户态） */
    .org HalExceptionVectors + 0x400
    b       HalExcLowerSync
    .org HalExceptionVectors + 0x480
    b       HalExcIrq
    .org HalExceptionVectors + 0x500
    b       HalExcUnexpected
    .org HalExceptionVectors + 0x580
    b       HalExcUnexpected

    /* Lower EL AArch32 */
    .org HalExceptionVectors + 0x600
    b       HalExcUnexpected
    .org HalExceptionVectors + 0x680
    b       HalExcUnexpected
    .org HalExceptionVectors + 0x700
    b       HalExcUnexpected
    .org HalExceptionVectors + 0x780
    b       HalExcUnexpected

    .align 2
HalExcUnexpected:
    mov     x0, #0
    bl      HalExceptionUnexpected
1:  wfi
    b       1b

/*
 * 内核同步异常：保存易失寄存器，调 HalExceptionSync(esr, far, elr)
 */
HalExcSync:
    sub     sp, sp, #256
    stp     x0, x1, [sp, #0]
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    stp     x8, x9, [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]
    str     x30, [sp, #240]

    mrs     x0, esr_el1
    mrs     x1, far_el1
    mrs     x2, elr_el1
    bl      HalExceptionSync
    cbz     x0, 2f
    msr     elr_el1, x0
2:
    ldr     x30, [sp, #240]
    ldp     x28, x29, [sp, #224]
    ldp     x26, x27, [sp, #208]
    ldp     x24, x25, [sp, #192]
    ldp     x22, x23, [sp, #176]
    ldp     x20, x21, [sp, #160]
    ldp     x18, x19, [sp, #144]
    ldp     x16, x17, [sp, #128]
    ldp     x14, x15, [sp, #112]
    ldp     x12, x13, [sp, #96]
    ldp     x10, x11, [sp, #80]
    ldp     x8, x9, [sp, #64]
    ldp     x6, x7, [sp, #48]
    ldp     x4, x5, [sp, #32]
    ldp     x2, x3, [sp, #16]
    ldp     x0, x1, [sp, #0]
    add     sp, sp, #256
    eret

/*
 * PR-A13：IRQ（Current EL 或 Lower EL）— 保存易失寄存器，调 HalExceptionIrq。
 * 用户态抢占仍靠协作 drain（A12）；此处保证 tick + WFI 唤醒。
 */
HalExcIrq:
    sub     sp, sp, #256
    stp     x0, x1, [sp, #0]
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    stp     x8, x9, [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]
    str     x30, [sp, #240]

    bl      HalExceptionIrq

    ldr     x30, [sp, #240]
    ldp     x28, x29, [sp, #224]
    ldp     x26, x27, [sp, #208]
    ldp     x24, x25, [sp, #192]
    ldp     x22, x23, [sp, #176]
    ldp     x20, x21, [sp, #160]
    ldp     x18, x19, [sp, #144]
    ldp     x16, x17, [sp, #128]
    ldp     x14, x15, [sp, #112]
    ldp     x12, x13, [sp, #96]
    ldp     x10, x11, [sp, #80]
    ldp     x8, x9, [sp, #64]
    ldp     x6, x7, [sp, #48]
    ldp     x4, x5, [sp, #32]
    ldp     x2, x3, [sp, #16]
    ldp     x0, x1, [sp, #0]
    add     sp, sp, #256
    eret

/*
 * Lower EL：在 SP_EL1 上压 HAL_FRAME，调 HalExceptionLower。
 * 返回 0 → 恢复本帧 eret；非 0 → 切到新帧（调度标签同 x86）。
 */
HalExcLowerSync:
    sub     sp, sp, #304
    stp     x0, x1, [sp, #0]
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    stp     x8, x9, [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]
    str     x30, [sp, #240]

    mrs     x0, elr_el1
    str     x0, [sp, #264]          /* Rip */
    mrs     x0, spsr_el1
    str     x0, [sp, #280]          /* Rflags */
    mrs     x0, sp_el0
    str     x0, [sp, #288]          /* Rsp */
    mov     x0, #0
    str     x0, [sp, #248]          /* Vec（C 填） */
    str     x0, [sp, #256]          /* Err */
    str     x0, [sp, #272]          /* Cs */
    str     x0, [sp, #296]          /* Ss */

    mov     x0, sp                  /* HAL_FRAME*；304 字节对齐友好 */
    bl      HalExceptionLower

    /* 自测 exit 经 HalUserSelfTestReturn；调度切换：低 2 位标签 */
    tst     x0, x0
    b.eq    .Llower_restore
    tbnz    x0, #0, .Llower_kern_first
    and     x0, x0, #0xfffffffffffffffc
    mov     sp, x0
    b       .Llower_restore

.Llower_kern_first:
    and     x0, x0, #0xfffffffffffffffc
    mov     x1, x0
    ldr     x2, [x1, #264]          /* Entry Rip */
    ldr     x3, [x1, #288]          /* Stack */
    mov     sp, x3
    br      x2

.Llower_restore:
    ldr     x0, [sp, #264]
    msr     elr_el1, x0
    ldr     x0, [sp, #280]
    msr     spsr_el1, x0
    ldr     x0, [sp, #288]
    msr     sp_el0, x0

    ldr     x30, [sp, #240]
    ldp     x28, x29, [sp, #224]
    ldp     x26, x27, [sp, #208]
    ldp     x24, x25, [sp, #192]
    ldp     x22, x23, [sp, #176]
    ldp     x20, x21, [sp, #160]
    ldp     x18, x19, [sp, #144]
    ldp     x16, x17, [sp, #128]
    ldp     x14, x15, [sp, #112]
    ldp     x12, x13, [sp, #96]
    ldp     x10, x11, [sp, #80]
    ldp     x8, x9, [sp, #64]
    ldp     x6, x7, [sp, #48]
    ldp     x4, x5, [sp, #32]
    ldp     x2, x3, [sp, #16]
    ldp     x0, x1, [sp, #0]
    add     sp, sp, #304
    eret

    .global HalUserSelfTestEnter
    .type HalUserSelfTestEnter, %function
    /* void HalUserSelfTestEnter(UINT64 Ksp, HAL_FRAME *Frame) */
HalUserSelfTestEnter:
    /* 保存全部 callee-saved + 原 SP 到 gUserSelfRet 扩展区 */
    adrp    x2, gUserSelfRet
    add     x2, x2, :lo12:gUserSelfRet
    mov     x3, sp
    str     x3, [x2, #0]
    str     x30, [x2, #8]
    stp     x19, x20, [x2, #16]
    stp     x21, x22, [x2, #32]
    stp     x23, x24, [x2, #48]
    stp     x25, x26, [x2, #64]
    stp     x27, x28, [x2, #80]
    str     x29, [x2, #96]

    mov     sp, x0                  /* Ksp */
    mov     x0, x1                  /* Frame* */
    b       HalUserEnter            /* 不返回；exit 走 HalUserSelfTestReturn */

    .global HalUserEnter
    .type HalUserEnter, %function
HalUserEnter:
    /* x0 = Frame*；进 EL0（返回点已由 SelfTestEnter 或调度路径准备） */
    ldr     x2, [x0, #264]
    msr     elr_el1, x2
    ldr     x2, [x0, #280]
    msr     spsr_el1, x2
    ldr     x2, [x0, #288]
    msr     sp_el0, x2
    ldp     x2, x3, [x0, #16]
    ldp     x4, x5, [x0, #32]
    ldp     x6, x7, [x0, #48]
    ldp     x8, x9, [x0, #64]
    ldp     x10, x11, [x0, #80]
    ldp     x12, x13, [x0, #96]
    ldp     x14, x15, [x0, #112]
    ldp     x16, x17, [x0, #128]
    ldp     x18, x19, [x0, #144]
    ldp     x20, x21, [x0, #160]
    ldp     x22, x23, [x0, #176]
    ldp     x24, x25, [x0, #192]
    ldp     x26, x27, [x0, #208]
    ldp     x28, x29, [x0, #224]
    ldr     x30, [x0, #240]
    ldp     x0, x1, [x0, #0]
    eret

    /* PR-A12：与 SelfTestEnter/Return 同实现（协作 drain 复用返回槽） */
    .global HalUserCoopEnter
    .type HalUserCoopEnter, %function
HalUserCoopEnter:
    b       HalUserSelfTestEnter

    .global HalUserCoopReturn
    .type HalUserCoopReturn, %function
HalUserCoopReturn:
    b       HalUserSelfTestReturn

    .global HalUserSelfTestReturn
    .type HalUserSelfTestReturn, %function
HalUserSelfTestReturn:
    adrp    x0, gUserSelfRet
    add     x0, x0, :lo12:gUserSelfRet
    ldr     x1, [x0, #0]
    ldr     x30, [x0, #8]
    ldp     x19, x20, [x0, #16]
    ldp     x21, x22, [x0, #32]
    ldp     x23, x24, [x0, #48]
    ldp     x25, x26, [x0, #64]
    ldp     x27, x28, [x0, #80]
    ldr     x29, [x0, #96]
    mov     sp, x1
    ret

    .size HalExceptionVectors, .-HalExceptionVectors

    .bss
    .align 3
    .global gUserSelfRet
gUserSelfRet:
    .space 112
