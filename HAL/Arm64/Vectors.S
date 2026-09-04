/*
 * Exception.S — PR-A10：EL1 向量表（同步异常 → C 缺页路径）
 * VBAR_EL1 要求 2KiB 对齐；每槽 0x80 字节。
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

    /* Current EL with SP_ELx */
    .org HalExceptionVectors + 0x200
    b       HalExcSync
    .org HalExceptionVectors + 0x280
    b       HalExcUnexpected
    .org HalExceptionVectors + 0x300
    b       HalExcUnexpected
    .org HalExceptionVectors + 0x380
    b       HalExcUnexpected

    /* Lower EL AArch64 */
    .org HalExceptionVectors + 0x400
    b       HalExcUnexpected
    .org HalExceptionVectors + 0x480
    b       HalExcUnexpected
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
 * 同步异常：保存易失寄存器，调 HalExceptionSync(esr, far, elr)
 * 返回值：新 ELR（probe 跳过指令时）或 0=保持原 ELR
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
    .size HalExceptionVectors, .-HalExceptionVectors
