#include <toyos/syscall.h>
/* user/SysHello.S — Ring 3：用 syscall/sysret 路径 write + exit（对照 HELLO.ELF 的 int 0x80） */
    .intel_syntax noprefix
    .section .text
    .global _start
_start:
    mov rax, SYS_WRITE
    mov rdi, 1
    lea rsi, [rip + msg]
    mov rdx, 14
    syscall

    mov rax, SYS_EXIT
    mov rdi, 0
    syscall

    .section .rodata
msg:
    .ascii "Hello SYSCALL!\n"
