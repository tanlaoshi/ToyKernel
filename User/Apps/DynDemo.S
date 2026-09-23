#include <toyos/syscall.h>
/* user/DynDemo.S — 动态链接演示：调用 LIBTOY.SO 中的 toy_hello */
    .intel_syntax noprefix
    .section .text
    .global _start
    .extern toy_hello
_start:
    call toy_hello
    mov rax, SYS_WRITE
    mov rdi, 1
    lea rsi, [rip + msg_done]
    mov rdx, 5
    int 0x80
    mov rax, SYS_EXIT
    xor rdi, rdi
    int 0x80

    .section .rodata
msg_done:
    .ascii "done\n"
