/* user/waitnh.S — fork + WNOHANG：父轮询 wait/yield，子退出后再 done */
    .intel_syntax noprefix
    .section .text
    .global _start
_start:
    mov rax, 5          /* SYS_FORK */
    int 0x80
    cmp rax, 0
    je .Lchild
    jl .Lfail

    /* parent */
    mov rax, 1
    mov rdi, 1
    lea rsi, [rip + msg_p]
    mov rdx, 2
    int 0x80

.Lpoll:
    mov rax, 6          /* SYS_WAIT */
    mov rdi, 1          /* WNOHANG */
    int 0x80
    cmp rax, 0
    jg .Lreaped        /* >0：收到子进程 */
    jl .Lfail           /* <0：无子进程 */
    /* ==0：子仍在跑 → yield 后再试 */
    mov rax, 7          /* SYS_YIELD */
    int 0x80
    jmp .Lpoll

.Lreaped:
    mov rax, 1
    mov rdi, 1
    lea rsi, [rip + msg_done]
    mov rdx, 5
    int 0x80
    mov rax, 0
    xor rdi, rdi
    int 0x80

.Lchild:
    mov rax, 1
    mov rdi, 1
    lea rsi, [rip + msg_c]
    mov rdx, 2
    int 0x80
    /* 多 yield 几次，让父有机会看到 WNOHANG 返回 0 */
    mov rcx, 8
.Lyields:
    mov rax, 7
    int 0x80
    dec rcx
    jnz .Lyields
    mov rax, 0
    mov rdi, 7
    int 0x80

.Lfail:
    mov rax, 1
    mov rdi, 1
    lea rsi, [rip + msg_fail]
    mov rdx, 5
    int 0x80
    mov rax, 0
    mov rdi, 1
    int 0x80

    .section .rodata
msg_p:
    .ascii "P\n"
msg_c:
    .ascii "C\n"
msg_done:
    .ascii "done\n"
msg_fail:
    .ascii "fail\n"
