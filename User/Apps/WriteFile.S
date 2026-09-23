#include <toyos/syscall.h>
/* user/WriteFile.S — 旧 int 0x80 对照；CRT 版见 WriteFile.c（WRITE.ELF） */
    .intel_syntax noprefix
    .section .text
    .global _start
_start:
    mov rax, SYS_OPEN              /* SYS_OPEN */
    lea rdi, [rip + path]
    int 0x80
    cmp rax, 0
    jl .Lfail
    mov rbx, rax            /* fd */

    mov rax, SYS_WRITE              /* SYS_WRITE */
    mov rdi, rbx
    lea rsi, [rip + msg]
    mov rdx, 16
    int 0x80
    cmp rax, 0
    jle .Lfail

    mov rax, SYS_CLOSE              /* SYS_CLOSE */
    mov rdi, rbx
    int 0x80

    mov rax, SYS_WRITE              /* confirm on stdout */
    mov rdi, 1
    lea rsi, [rip + ok]
    mov rdx, 3
    int 0x80

    mov rax, SYS_EXIT
    xor rdi, rdi
    int 0x80

.Lfail:
    mov rax, SYS_WRITE
    mov rdi, 1
    lea rsi, [rip + err]
    mov rdx, 4
    int 0x80
    mov rax, SYS_EXIT
    mov rdi, 1
    int 0x80

    .section .rodata
path:
    .asciz "USERNOTE.TXT"
msg:
    .ascii "from ring3!\n"
ok:
    .ascii "ok\n"
err:
    .ascii "err\n"
