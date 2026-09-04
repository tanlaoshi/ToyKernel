/* User/crt/syscall.S — long toy_syscall(n, a, b, c) via syscall */
    .intel_syntax noprefix
    .section .text
    .global toy_syscall
toy_syscall:
    mov rax, rdi
    mov rdi, rsi
    mov rsi, rdx
    mov rdx, rcx
    syscall
    ret
