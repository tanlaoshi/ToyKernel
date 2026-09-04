/* User/crt/crt0.S — _start → main(argc,argv) → exit（syscall）
 * 栈布局（execve / 空栈）：[rsp]=argc, [rsp+8]=argv[0]…（页已清零时 argc=0）
 */
    .intel_syntax noprefix
    .section .text
    .global _start
    .extern main
_start:
    mov rdi, [rsp]
    lea rsi, [rsp + 8]
    call main
    mov rdi, rax
    mov rax, 0
    xor rsi, rsi
    xor rdx, rdx
    syscall
1:
    jmp 1b
