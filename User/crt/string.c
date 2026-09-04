#include "string.h"

size_t strlen(const char *s) {
    size_t n = 0;
    if (!s) {
        return 0;
    }
    while (s[n]) {
        n++;
    }
    return n;
}

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *D = (unsigned char *)dst;
    const unsigned char *S = (const unsigned char *)src;
    size_t i;
    for (i = 0; i < n; i++) {
        D[i] = S[i];
    }
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    unsigned char *D = (unsigned char *)dst;
    size_t i;
    for (i = 0; i < n; i++) {
        D[i] = (unsigned char)c;
    }
    return dst;
}
