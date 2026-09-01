/* user/fork.S — fork + 阻塞 wait：父打印 P，子打印 C，父 wait 后再 done */
    .intel_syntax noprefix
    .section .text
    .global _start
_start:
    mov rax, 5          /* SYS_FORK */
    int 0x80
    cmp rax, 0
    je .Lchild
    jl .Lfail

    /* parent: print "P\n" then blocking wait */
    mov rax, 1
    mov rdi, 1
    lea rsi, [rip + msg_p]
    mov rdx, 2
    int 0x80

    mov rax, 6          /* SYS_WAIT — blocks until child exits */
    int 0x80
    cmp rax, 0
    jl .Lfail

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
    mov rax, 0
    mov rdi, 42
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
