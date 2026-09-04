/* User/crt/crt0.S — _start → main → exit（syscall） */
    .intel_syntax noprefix
    .section .text
    .global _start
    .extern main
_start:
    call main
    mov rdi, rax
    mov rax, 0
    xor rsi, rsi
    xor rdx, rdx
    syscall
    /* 若 exit 返回则空转 */
1:
    jmp 1b
