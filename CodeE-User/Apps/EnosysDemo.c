/*
 * EnosysDemo.c — 开课前：未知 syscall 须返回 -ENOSYS（38）
 */
#include <errno.h>
#include <stdio.h>
#include <toyos/syscall.h>

#define BAD_SYS 9999

int main(void) {
    long R;

    R = toy_syscall(BAD_SYS, 0, 0, 0);
    if (R == -(long)ENOSYS) {
        printf("enosys: ok r=%d\n", (int)R);
        return 0;
    }
    printf("enosys: FAIL r=%d want %d\n", (int)R, -ENOSYS);
    return 1;
}
