#include <toyos/syscall.h>
/* user/sysFork.S — fork + wait 走 syscall（对照 FORK.ELF 的 int 0x80） */
    .intel_syntax noprefix
    .section .text
    .global _start
_start:
    mov rax, SYS_FORK          /* SYS_FORK */
    syscall
    cmp rax, 0
    je .Lchild
    jl .Lfail

    mov rax, SYS_WRITE
    mov rdi, 1
    lea rsi, [rip + msg_p]
    mov rdx, 2
    syscall

    mov rax, SYS_WAIT          /* SYS_WAIT */
    xor rdi, rdi
    syscall
    cmp rax, 0
    jl .Lfail

    mov rax, SYS_WRITE
    mov rdi, 1
    lea rsi, [rip + msg_done]
    mov rdx, 5
    syscall
    mov rax, SYS_EXIT
    xor rdi, rdi
    syscall

.Lchild:
    mov rax, SYS_WRITE
    mov rdi, 1
    lea rsi, [rip + msg_c]
    mov rdx, 2
    syscall
    mov rax, SYS_EXIT
    mov rdi, 42
    syscall

.Lfail:
    mov rax, SYS_WRITE
    mov rdi, 1
    lea rsi, [rip + msg_fail]
    mov rdx, 5
    syscall
    mov rax, SYS_EXIT
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
