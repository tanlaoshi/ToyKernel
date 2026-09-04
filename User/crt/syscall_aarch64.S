/* User/crt/syscall_aarch64.S — long toy_syscall(n,a,b,c) via SVC */
	.section .text
	.global toy_syscall
toy_syscall:
	mov	x8, x0
	mov	x0, x1
	mov	x1, x2
	mov	x2, x3
	svc	#0
	ret
