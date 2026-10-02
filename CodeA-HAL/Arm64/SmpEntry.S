/*
 * SmpEntry.S — PR-A14：PSCI CPU_ON 入口（x0=logical cpu）
 */
    .section .text, "ax"
    .global HalApEntry
    .type HalApEntry, %function
HalApEntry:
    /* x0 = context_id = logical CPU（BSP 传入） */
    mov     x19, x0

    adrp    x1, gApStackTop
    add     x1, x1, :lo12:gApStackTop
    ldr     x2, [x1, x19, lsl #3]
    mov     sp, x2

    mov     x0, x19
    bl      HalApMain
1:  wfi
    b       1b
    .size HalApEntry, .-HalApEntry
