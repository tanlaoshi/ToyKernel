/*
 * Isr.S — x86-64 中断入口桩（汇编）
 *
 * 宏 ISR_NOERR / ISR_ERR 生成向量 0–31（CPU 异常）与 32–47（8259 PIC）入口。
 * 向量 64/65 分别用于 XHCI 与 LAPIC 定时器；255 为默认未使用向量。
 *
 * isr_common：保存全部 GPR → 调用 InterruptDispatch → 若 rax≠0 则切换 rsp → iretq
 * SchedEnter：首次进入任务，直接恢复 Frame 并 iretq（不经中断）
 */
.intel_syntax noprefix
.altmacro

/* 无错误码的 ISR：压入假错误码 0 与向量号 */
.macro ISR_NOERR vec
.global isr_\vec
isr_\vec:
    push 0
    push \vec
    jmp isr_common
.endm

/* 带错误码的 ISR：仅压入向量号（CPU 已压入错误码） */
.macro ISR_ERR vec
.global isr_\vec
isr_\vec:
    push \vec
    jmp isr_common
.endm

.section .text

/* CPU 异常向量 0–31 */
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR 8
ISR_NOERR 9
ISR_ERR 10
ISR_ERR 11
ISR_ERR 12
ISR_ERR 13
ISR_ERR 14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR 17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

/* 8259 PIC IRQ 向量 32–47 */
.set i, 32
.rept 16
    ISR_NOERR %i
    .set i, i+1
.endr

/* 自定义：64=XHCI MSI-X，65=LAPIC 定时器，128=系统调用，255=默认 */
ISR_NOERR 64
ISR_NOERR 65
ISR_NOERR 128
ISR_NOERR 255

.extern InterruptDispatch

/* 公共中断路径：保存寄存器 → C 分发 → 可选上下文切换 → 恢复并 iretq */
.global isr_common
isr_common:
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax
    mov rdi, rsp
    call InterruptDispatch
    test rax, rax
    jz 1f
    bt rax, 63
    jc 2f
    bt rax, 62
    jc 3f
    mov rsp, rax
    jmp 1f
2:
    btr rax, 63
    mov rdi, rax
    jmp KernelEnter
3:
    btr rax, 62
    mov rdi, rax
    jmp UserEnter
1:
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
    add rsp, 16
    iretq

/* 内核任务首次进入：rdi=INT_FRAME*，直接 jmp Rip 并切换 RSP */
.global KernelEnter
KernelEnter:
    mov rax, [rdi + 136]    /* Rip */
    mov rsp, [rdi + 160]    /* Rsp */
    jmp rax

/* 用户态首次进入：rdi=INT_FRAME*，构造 iretq 帧 */
.global UserEnter
UserEnter:
    mov rax, [rdi + 136]    /* Rip */
    mov rcx, [rdi + 144]    /* Cs */
    mov rdx, [rdi + 152]    /* Rflags */
    mov r11, [rdi + 160]    /* Rsp */
    mov r10, [rdi + 168]    /* Ss */
    mov rsp, r11
    sub rsp, 40
    mov [rsp], rax
    mov [rsp + 8], rcx
    mov [rsp + 16], rdx
    mov [rsp + 24], r11
    mov [rsp + 32], r10
    iretq

.section .rodata
/* 异常处理函数指针表，供 Arch.c IdtLoad 使用 */
.global IsrException
IsrException:
    .quad isr_0,  isr_1,  isr_2,  isr_3,  isr_4,  isr_5,  isr_6,  isr_7
    .quad isr_8,  isr_9,  isr_10, isr_11, isr_12, isr_13, isr_14, isr_15
    .quad isr_16, isr_17, isr_18, isr_19, isr_20, isr_21, isr_22, isr_23
    .quad isr_24, isr_25, isr_26, isr_27, isr_28, isr_29, isr_30, isr_31

/* PIC IRQ 处理函数指针表 */
.global IsrPic
IsrPic:
    .quad isr_32, isr_33, isr_34, isr_35, isr_36, isr_37, isr_38, isr_39
    .quad isr_40, isr_41, isr_42, isr_43, isr_44, isr_45, isr_46, isr_47

.global SchedEnter
SchedEnter:
    jmp KernelEnter
