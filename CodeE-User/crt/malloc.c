/*
 * malloc.c — PR-P3：经 SYS_BRK 扩展堆；PR-A-libc：块头记录 size 供 realloc
 */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    size_t size;
} MALLOC_HDR;

void *malloc(size_t n) {
    size_t Align = 8;
    size_t Need;
    void *Old;
    void *Neu;
    MALLOC_HDR *Hdr;

    if (n == 0) {
        return 0;
    }
    Need = sizeof(MALLOC_HDR) + ((n + Align - 1) & ~(Align - 1));
    Old = brk((void *)0);
    if (Old == (void *)(long)-1) {
        return 0;
    }
    Neu = (void *)((char *)Old + Need);
    if (brk(Neu) != Neu) {
        return 0;
    }
    Hdr = (MALLOC_HDR *)Old;
    Hdr->size = n;
    return Hdr + 1;
}

void *calloc(size_t nmemb, size_t size) {
    size_t N;
    void *P;

    if (nmemb == 0 || size == 0) {
        return 0;
    }
    if (size != 0 && nmemb > ((size_t)-1) / size) {
        return 0;
    }
    N = nmemb * size;
    P = malloc(N);
    if (P) {
        memset(P, 0, N);
    }
    return P;
}

void *realloc(void *p, size_t n) {
    MALLOC_HDR *Hdr;
    void *Q;
    size_t Old;

    if (!p) {
        return malloc(n);
    }
    if (n == 0) {
        free(p);
        return 0;
    }
    Hdr = (MALLOC_HDR *)p - 1;
    Old = Hdr->size;
    if (n <= Old) {
        Hdr->size = n;
        return p;
    }
    Q = malloc(n);
    if (!Q) {
        return 0;
    }
    memcpy(Q, p, Old);
    free(p);
    return Q;
}

void free(void *p) {
    (void)p;
    /* bump：CRT 不回收；可 brk 收缩但教学省略 */
}
