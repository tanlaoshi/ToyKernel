/*
 * malloc.c — BSS 固定堆（无 brk/mmap；CRT1 教学用）
 * 后续 PR 可用 SYS_BRK 扩展。
 */
#include "stdlib.h"
#include "string.h"

#define HEAP_SIZE (8 * 1024)

static unsigned char gHeap[HEAP_SIZE];
static size_t gHeapUsed;

void *malloc(size_t n) {
    void *P;
    size_t Align = 8;
    size_t Need;

    if (n == 0) {
        return 0;
    }
    Need = (n + Align - 1) & ~(Align - 1);
    if (gHeapUsed + Need > HEAP_SIZE) {
        return 0;
    }
    P = &gHeap[gHeapUsed];
    gHeapUsed += Need;
    return P;
}

void free(void *p) {
    (void)p;
    /* bump 分配器：CRT1 不回收 */
}

void exit(int status) {
    toy_exit(status);
    for (;;) {
    }
}
