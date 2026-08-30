/* user/count.S — 打印 0-9 每行一个数字，验证第二个 ELF 程序 */
    .intel_syntax noprefix
    .section .text
    .global _start
_start:
    xor rbx, rbx
.Lloop:
    cmp rbx, 10
    jge .Ldone
    mov rax, 48
    add rax, rbx
    mov [rsp-2], al
    mov byte ptr [rsp-1], 10
    mov rax, 1
    mov rdi, 1
    lea rsi, [rsp-2]
    mov rdx, 2
    int 0x80
    inc rbx
    jmp .Lloop
.Ldone:
    mov rax, 0
    xor rdi, rdi
    int 0x80
