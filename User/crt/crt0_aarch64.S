/* User/crt/crt0_aarch64.S — PR-A12：_start → main → exit(SVC) */
	.section .text
	.global _start
	.extern main
_start:
	ldr	x0, [sp]
	add	x1, sp, #8
	bl	main
	mov	x8, #0			/* SYS_EXIT */
	mov	x1, xzr
	mov	x2, xzr
	svc	#0
1:	b	1b
