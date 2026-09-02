/* user/netsrv.S — bind/listen/accept echo once (needs LWIP=1, hostfwd :9000)
 *
 * 流程：打印 listen → accept 阻塞 → 宿主机 echo hi | nc 127.0.0.1 9000 → ok
 */
    .intel_syntax noprefix
    .section .text
    .global _start
_start:
    sub rsp, 80
    mov rbx, -1
    mov r12, -1

    /* socket(AF_INET, SOCK_STREAM, 0) */
    mov rax, 8
    mov rdi, 2
    mov rsi, 1
    xor rdx, rdx
    int 0x80
    cmp rax, 0
    jl .Lfail
    mov rbx, rax

    /* bind(fd, INADDR_ANY, 9000) */
    mov rax, 10
    mov rdi, rbx
    xor rsi, rsi
    mov rdx, 9000
    int 0x80
    cmp rax, 0
    jl .Lfail_bind

    /* listen(fd, 1) */
    mov rax, 11
    mov rdi, rbx
    mov rsi, 1
    int 0x80
    cmp rax, 0
    jl .Lfail_listen

    /* tell user we are waiting */
    mov rax, 1
    mov rdi, 1
    lea rsi, [rip + waitmsg]
    mov rdx, 12
    int 0x80

    /* accept(fd) — blocks until connection */
    mov rax, 12
    mov rdi, rbx
    int 0x80
    cmp rax, 0
    jl .Lfail_accept
    mov r12, rax

    /* read(client, buf, 64) */
    mov rax, 3
    mov rdi, r12
    mov rsi, rsp
    mov rdx, 64
    int 0x80
    cmp rax, 0
    jle .Lfail_client
    mov rcx, rax

    /* echo */
    mov rax, 1
    mov rdi, r12
    mov rsi, rsp
    mov rdx, rcx
    int 0x80
    cmp rax, 0
    jle .Lfail_client

    mov rax, 4
    mov rdi, r12
    int 0x80
    mov rax, 4
    mov rdi, rbx
    int 0x80

    mov rax, 1
    mov rdi, 1
    lea rsi, [rip + ok]
    mov rdx, 3
    int 0x80

    mov rax, 0
    xor rdi, rdi
    int 0x80

.Lfail_bind:
    mov rax, 1
    mov rdi, 1
    lea rsi, [rip + ebind]
    mov rdx, 6
    int 0x80
    jmp .Lfail_close
.Lfail_listen:
    mov rax, 1
    mov rdi, 1
    lea rsi, [rip + elisten]
    mov rdx, 8
    int 0x80
    jmp .Lfail_close
.Lfail_accept:
    mov rax, 1
    mov rdi, 1
    lea rsi, [rip + eaccept]
    mov rdx, 8
    int 0x80
    jmp .Lfail_close
.Lfail_client:
    mov rax, 1
    mov rdi, 1
    lea rsi, [rip + erecv]
    mov rdx, 6
    int 0x80
    cmp r12, 0
    jl .Lfail_close
    mov rax, 4
    mov rdi, r12
    int 0x80
    jmp .Lfail_close
.Lfail:
    mov rax, 1
    mov rdi, 1
    lea rsi, [rip + esock]
    mov rdx, 27
    int 0x80
.Lfail_close:
    cmp rbx, 0
    jl .Lexit_err
    mov rax, 4
    mov rdi, rbx
    int 0x80
.Lexit_err:
    mov rax, 0
    mov rdi, 1
    int 0x80

    .section .rodata
waitmsg:
    .ascii "listen:9000\n"
ok:
    .ascii "ok\n"
esock:
    .ascii "esock (need LWIP=1 kernel)\n"
ebind:
    .ascii "ebind\n"
elisten:
    .ascii "elisten\n"
eaccept:
    .ascii "eaccept\n"
erecv:
    .ascii "erecv\n"
