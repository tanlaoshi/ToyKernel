/*
 * SyscallEntry.S — SYSCALL/SYSRET 快速路径（与 int 0x80 / Isr128 解耦）
 *
 * 入口：用户态 syscall → 本桩 → 组装与 IsrCommon 相同的 HAL_FRAME → SyscallDispatch
 * 返回：同任务 SYSRET；任务切换则走 iretq / KernelEnter（与 Isr.S 语义对齐）
 *
 * 依赖：ArchSyscallMsrInit 已设 LSTAR/STAR/SFMASK/EFER.SCE，
 *       KERNEL_GS_BASE → ARCH_CPU_LOCAL（[0]=KernelRsp，[8]=ScratchUserRsp）
 *
 * ABI：rax=号，rdi/rsi/rdx=参数（与 int 0x80 相同）。rcx/r11 由硬件改写。
 */
.intel_syntax noprefix
.section .text

.extern SyscallDispatch
.extern KernelEnter

.global SyscallEntry
SyscallEntry:
    swapgs
    mov qword ptr gs:[8], rsp          /* 用户 RSP → per-CPU scratch */
    mov rsp, qword ptr gs:[0]          /* 切到 TSS.RSP0 / ArchSetRsp0 */

    /* 伪造与特权级中断相同的尾部：SS RSP RFLAGS CS RIP + Error + Vector */
    push 0x1B                          /* 用户 SS */
    push qword ptr gs:[8]              /* 用户 RSP */
    push r11                           /* RFLAGS（SYSCALL 存入 r11） */
    push 0x23                          /* 用户 CS */
    push rcx                           /* RIP（SYSCALL 存入 rcx） */
    push 0                             /* ErrorCode */
    push 0x80                          /* Vector 标记（仅填满帧；不经 IDT） */

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
    /* call 前 16 字节对齐（与 IsrCommon 相同） */
    mov rbx, rsp
    sub rsp, 8
    and rsp, -16
    call SyscallDispatch
    mov rsp, rbx

    test rax, rax
    jz .Lsysret

    /* 非 0：与 IsrCommon 相同的切换标签 */
    test al, 1
    jnz .Lkernel_first
    test al, 2
    jnz .Luser_first
    and rax, -4
    mov rsp, rax
    jmp .Liret
.Luser_first:
    and rax, -4
    mov rsp, rax
    jmp .Liret
.Lkernel_first:
    and rax, -4
    mov rdi, rax
    swapgs
    jmp KernelEnter

.Lsysret:
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
    add rsp, 16                        /* Vector + ErrorCode */
    pop rcx                            /* RIP → SYSRET */
    add rsp, 8                         /* 跳过 CS（STAR 决定） */
    pop r11                            /* RFLAGS → SYSRET */
    pop r10                            /* 用户 RSP */
    add rsp, 8                         /* 跳过 SS */
    swapgs
    mov rsp, r10
    sysretq

.Liret:
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
    swapgs
    iretq
