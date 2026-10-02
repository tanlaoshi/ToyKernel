#include <toyos/syscall.h>
/* user/CatFile.S — 旧 int 0x80 对照；CRT 版见 Cat.c（CAT.ELF） */
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

.Lloop:
    mov rax, SYS_READ              /* SYS_READ */
    mov rdi, rbx
    lea rsi, [rsp - 64]
    mov rdx, 32
    int 0x80
    cmp rax, 0
    jle .Lclose
    mov rdx, rax
    mov rax, SYS_WRITE              /* SYS_WRITE */
    mov rdi, 1
    lea rsi, [rsp - 64]
    int 0x80
    jmp .Lloop

.Lclose:
    mov rax, SYS_CLOSE              /* SYS_CLOSE */
    mov rdi, rbx
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
    .asciz "TOYOS.ID"
err:
    .ascii "err\n"
