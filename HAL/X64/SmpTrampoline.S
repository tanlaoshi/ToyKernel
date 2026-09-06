/*
 * SmpTrampoline.S — AP 实模式 → 长模式（链接地址 0x8000，由 BSP 拷贝）
 *
 * 启动参数在 0x7E00（见 SmpBoot.c SMP_BOOT_INFO）：
 *   +0  cr3
 *   +8  stack_top
 *   +16 ap_entry (64-bit)
 * GDT 由 BSP 写在 0x7F00，GDTR 在 0x7EF0。
 */
    .code16
    .section .text
    .global SmpTrampolineStart
    .global SmpTrampolineEnd
SmpTrampolineStart:
    cli
    xor     %ax, %ax
    mov     %ax, %ds
    mov     %ax, %es
    mov     %ax, %ss
    mov     $0x7000, %sp

    lgdtl   0x7EF0

    mov     %cr0, %eax
    or      $1, %eax
    mov     %eax, %cr0
    ljmpl   $0x08, $SmpProt32

    .code32
SmpProt32:
    mov     $0x10, %ax
    mov     %ax, %ds
    mov     %ax, %es
    mov     %ax, %ss
    mov     $0x7000, %esp

    /* CR3 */
    mov     0x7E00, %eax
    mov     %eax, %cr3

    /* CR4.PAE */
    mov     %cr4, %eax
    or      $(1 << 5), %eax
    mov     %eax, %cr4

    /* EFER.LME */
    mov     $0xC0000080, %ecx
    rdmsr
    or      $(1 << 8), %eax
    wrmsr

    /* CR0.PG */
    mov     %cr0, %eax
    or      $(1 << 31), %eax
    mov     %eax, %cr0

    ljmpl   $0x18, $SmpLong64

    .code64
SmpLong64:
    mov     $0x20, %ax
    mov     %ax, %ds
    mov     %ax, %es
    mov     %ax, %ss
    mov     %ax, %fs
    mov     %ax, %gs

    movabs  $0x7E00, %rax
    mov     8(%rax), %rsp
    mov     16(%rax), %rax
    call    *%rax

1:
    cli
    hlt
    jmp     1b

SmpTrampolineEnd:
