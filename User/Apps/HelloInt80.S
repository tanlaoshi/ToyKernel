#include <toyos/syscall.h>
/* user/HelloInt80.S — 旧版 Ring3：int 0x80 write + exit（教学对照；CRT 见 Hello.c） */
    .intel_syntax noprefix
    .section .text
    .global _start
_start:
    mov rax, SYS_WRITE
    mov rdi, 1
    lea rsi, [rip + msg]
    mov rdx, 13
    int 0x80

    mov rax, SYS_EXIT
    mov rdi, 0
    int 0x80

    .section .rodata
msg:
    .ascii "Hello Ring3!\n"
