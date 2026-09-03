/* user/syshello.S — Ring 3：用 syscall/sysret 路径 write + exit（对照 HELLO.ELF 的 int 0x80） */
    .intel_syntax noprefix
    .section .text
    .global _start
_start:
    mov rax, 1
    mov rdi, 1
    lea rsi, [rip + msg]
    mov rdx, 14
    syscall

    mov rax, 0
    mov rdi, 0
    syscall

    .section .rodata
msg:
    .ascii "Hello SYSCALL!\n"
