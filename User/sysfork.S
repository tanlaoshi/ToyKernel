/* user/sysfork.S — fork + wait 走 syscall（对照 FORK.ELF 的 int 0x80） */
    .intel_syntax noprefix
    .section .text
    .global _start
_start:
    mov rax, 5          /* SYS_FORK */
    syscall
    cmp rax, 0
    je .Lchild
    jl .Lfail

    mov rax, 1
    mov rdi, 1
    lea rsi, [rip + msg_p]
    mov rdx, 2
    syscall

    mov rax, 6          /* SYS_WAIT */
    xor rdi, rdi
    syscall
    cmp rax, 0
    jl .Lfail

    mov rax, 1
    mov rdi, 1
    lea rsi, [rip + msg_done]
    mov rdx, 5
    syscall
    mov rax, 0
    xor rdi, rdi
    syscall

.Lchild:
    mov rax, 1
    mov rdi, 1
    lea rsi, [rip + msg_c]
    mov rdx, 2
    syscall
    mov rax, 0
    mov rdi, 42
    syscall

.Lfail:
    mov rax, 1
    mov rdi, 1
    lea rsi, [rip + msg_fail]
    mov rdx, 5
    syscall
    mov rax, 0
    mov rdi, 1
    syscall

    .section .rodata
msg_p:
    .ascii "P\n"
msg_c:
    .ascii "C\n"
msg_done:
    .ascii "done\n"
msg_fail:
    .ascii "fail\n"
