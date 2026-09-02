/* user/libtoy.S — 共享库：导出 toy_hello，打印 from.so */
    .intel_syntax noprefix
    .section .rodata
toy_msg:
    .ascii "from.so\n"
    .section .text
    .global toy_hello
toy_hello:
    mov rax, 1
    mov rdi, 1
    lea rsi, [rip + toy_msg]
    mov rdx, 8
    int 0x80
    ret
