/* user/hello.S — Ring 3 演示：syscall write + exit */
    .intel_syntax noprefix
    .section .text
    .global _start
_start:
    mov rax, 1
    mov rdi, 1
    lea rsi, [rip + msg]
    mov rdx, 13
    int 0x80

    mov rax, 0
    mov rdi, 0
    int 0x80

    .section .rodata
msg:
    .ascii "Hello Ring3!\n"
