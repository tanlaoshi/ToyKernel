/* User/crt/crt0_riscv.S — PR-A12：_start → main → exit(ecall) */
	.section .text
	.global _start
	.extern main
_start:
	ld	a0, 0(sp)
	addi	a1, sp, 8
	call	main
	li	a7, 0			/* SYS_EXIT */
	ecall
1:	j	1b
