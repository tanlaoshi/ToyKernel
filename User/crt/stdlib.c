/*
 * stdlib.c — atoi / qsort / abs（PR-L2）；malloc 仍在 malloc.c
 */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

int abs(int x) {
    return x < 0 ? -x : x;
}

long labs(long x) {
    return x < 0 ? -x : x;
}

int atoi(const char *s) {
    return (int)atol(s);
}

long atol(const char *s) {
    long sign = 1;
    long v = 0;

    if (!s) {
        return 0;
    }
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '+' || *s == '-') {
        if (*s == '-') {
            sign = -1;
        }
        s++;
    }
    while (isdigit((unsigned char)*s)) {
        v = v * 10 + (*s - '0');
        s++;
    }
    return sign * v;
}

static void SwapBytes(unsigned char *a, unsigned char *b, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned char t = a[i];
        a[i] = b[i];
        b[i] = t;
    }
}

void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *)) {
    unsigned char *b;
    size_t i, j;

    if (!base || !compar || nmemb < 2 || size == 0) {
        return;
    }
    b = (unsigned char *)base;
    /* 教学用插入排序：稳、短，不依赖递归栈 */
    for (i = 1; i < nmemb; i++) {
        for (j = i; j > 0; j--) {
            unsigned char *left = b + (j - 1) * size;
            unsigned char *right = b + j * size;
            if (compar(left, right) <= 0) {
                break;
            }
            SwapBytes(left, right, size);
        }
    }
}
