/* User/crt/syscall_riscv.S — long toy_syscall(n,a,b,c) via ecall */
	.section .text
	.global toy_syscall
toy_syscall:
	mv	a7, a0
	mv	a0, a1
	mv	a1, a2
	mv	a2, a3
	ecall
	ret
