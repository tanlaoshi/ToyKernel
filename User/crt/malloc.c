/*
 * malloc.c — PR-P3：经 SYS_BRK 扩展堆（替代 BSS 8KiB bump）
 */
#include "stdlib.h"
#include "string.h"
#include "unistd.h"

void *malloc(size_t n) {
    size_t Align = 8;
    size_t Need;
    void *Old;
    void *Neu;

    if (n == 0) {
        return 0;
    }
    Need = (n + Align - 1) & ~(Align - 1);
    Old = brk((void *)0);
    if (Old == (void *)(long)-1) {
        return 0;
    }
    Neu = (void *)((char *)Old + Need);
    if (brk(Neu) != Neu) {
        return 0;
    }
    return Old;
}

void free(void *p) {
    (void)p;
    /* bump：CRT 不回收；可 brk 收缩但教学省略 */
}

void exit(int status) {
    toy_exit(status);
    for (;;) {
    }
}
