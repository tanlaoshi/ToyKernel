/* user/catfile.S — open/read TOYOS.ID 并 write 到控制台 */
    .intel_syntax noprefix
    .section .text
    .global _start
_start:
    mov rax, 2              /* SYS_OPEN */
    lea rdi, [rip + path]
    int 0x80
    cmp rax, 0
    jl .Lfail
    mov rbx, rax            /* fd */

.Lloop:
    mov rax, 3              /* SYS_READ */
    mov rdi, rbx
    lea rsi, [rsp - 64]
    mov rdx, 32
    int 0x80
    cmp rax, 0
    jle .Lclose
    mov rdx, rax
    mov rax, 1              /* SYS_WRITE */
    mov rdi, 1
    lea rsi, [rsp - 64]
    int 0x80
    jmp .Lloop

.Lclose:
    mov rax, 4              /* SYS_CLOSE */
    mov rdi, rbx
    int 0x80
    mov rax, 0
    xor rdi, rdi
    int 0x80

.Lfail:
    mov rax, 1
    mov rdi, 1
    lea rsi, [rip + err]
    mov rdx, 4
    int 0x80
    mov rax, 0
    mov rdi, 1
    int 0x80

    .section .rodata
path:
    .asciz "TOYOS.ID"
err:
    .ascii "err\n"
