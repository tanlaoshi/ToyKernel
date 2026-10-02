/* user/NetServer.S — bind/listen/accept echo once (needs LWIP=1, hostfwd :9000)
 *
 * 流程：打印 listen → accept 阻塞 → 宿主机 echo hi | nc 127.0.0.1 9000 → ok
 */
#include <toyos/syscall.h>
    .intel_syntax noprefix
    .section .text
    .global _start
_start:
    sub rsp, 80
    mov rbx, -1
    mov r12, -1

    /* socket(AF_INET, SOCK_STREAM, 0) */
    mov rax, SYS_SOCKET
    mov rdi, 2
    mov rsi, 1
    xor rdx, rdx
    int 0x80
    cmp rax, 0
    jl .Lfail
    mov rbx, rax

    /* bind(fd, INADDR_ANY, 9000) */
    mov rax, SYS_BIND
    mov rdi, rbx
    xor rsi, rsi
    mov rdx, 9000
    int 0x80
    cmp rax, 0
    jl .Lfail_bind

    /* listen(fd, 1) */
    mov rax, SYS_LISTEN
    mov rdi, rbx
    mov rsi, 1
    int 0x80
    cmp rax, 0
    jl .Lfail_listen

    /* tell user we are waiting */
    mov rax, SYS_WRITE
    mov rdi, 1
    lea rsi, [rip + waitmsg]
    mov rdx, 12
    int 0x80

    /* accept(fd) — blocks until connection */
    mov rax, SYS_ACCEPT
    mov rdi, rbx
    int 0x80
    cmp rax, 0
    jl .Lfail_accept
    mov r12, rax

    /* read(client, buf, 64) */
    mov rax, SYS_READ
    mov rdi, r12
    mov rsi, rsp
    mov rdx, 64
    int 0x80
    cmp rax, 0
    jle .Lfail_client
    mov rcx, rax

    /* echo */
    mov rax, SYS_WRITE
    mov rdi, r12
    mov rsi, rsp
    mov rdx, rcx
    int 0x80
    cmp rax, 0
    jle .Lfail_client

    mov rax, SYS_CLOSE
    mov rdi, r12
    int 0x80
    mov rax, SYS_CLOSE
    mov rdi, rbx
    int 0x80

    mov rax, SYS_WRITE
    mov rdi, 1
    lea rsi, [rip + ok]
    mov rdx, 3
    int 0x80

    mov rax, SYS_EXIT
    xor rdi, rdi
    int 0x80

.Lfail_bind:
    mov rax, SYS_WRITE
    mov rdi, 1
    lea rsi, [rip + ebind]
    mov rdx, 6
    int 0x80
    jmp .Lfail_close
.Lfail_listen:
    mov rax, SYS_WRITE
    mov rdi, 1
    lea rsi, [rip + elisten]
    mov rdx, 8
    int 0x80
    jmp .Lfail_close
.Lfail_accept:
    mov rax, SYS_WRITE
    mov rdi, 1
    lea rsi, [rip + eaccept]
    mov rdx, 8
    int 0x80
    jmp .Lfail_close
.Lfail_client:
    mov rax, SYS_WRITE
    mov rdi, 1
    lea rsi, [rip + erecv]
    mov rdx, 6
    int 0x80
    cmp r12, 0
    jl .Lfail_close
    mov rax, SYS_CLOSE
    mov rdi, r12
    int 0x80
    jmp .Lfail_close
.Lfail:
    mov rax, SYS_WRITE
    mov rdi, 1
    lea rsi, [rip + esock]
    mov rdx, 27
    int 0x80
.Lfail_close:
    cmp rbx, 0
    jl .Lexit_err
    mov rax, SYS_CLOSE
    mov rdi, rbx
    int 0x80
.Lexit_err:
    mov rax, SYS_EXIT
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
