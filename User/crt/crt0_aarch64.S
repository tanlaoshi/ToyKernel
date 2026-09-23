/* User/crt/crt0_aarch64.S — PR-A12：_start → main → exit(SVC)
 * 号段重排后 SYS_EXIT=50（勿再写死 0）
 */
#include <toyos/syscall.h>
	.section .text
	.global _start
	.extern main
_start:
	ldr	x0, [sp]
	add	x1, sp, #8
	bl	main
	/* x0 = main 返回值作退出码；x8 = SYS_EXIT */
	mov	x8, #SYS_EXIT
	mov	x1, xzr
	mov	x2, xzr
	svc	#0
1:	b	1b
