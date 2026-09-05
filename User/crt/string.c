#include <string.h>

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

int strcmp(const char *a, const char *b) {
    if (!a) {
        a = "";
    }
    if (!b) {
        b = "";
    }
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    size_t i;
    if (!a) {
        a = "";
    }
    if (!b) {
        b = "";
    }
    for (i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca != cb || ca == 0) {
            return ca - cb;
        }
    }
    return 0;
}

char *strcpy(char *dst, const char *src) {
    char *d = dst;
    if (!dst) {
        return dst;
    }
    if (!src) {
        *d = 0;
        return dst;
    }
    while ((*d++ = *src++) != 0) {
    }
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
    size_t i;
    if (!dst) {
        return dst;
    }
    if (!src) {
        src = "";
    }
    for (i = 0; i < n && src[i]; i++) {
        dst[i] = src[i];
    }
    for (; i < n; i++) {
        dst[i] = 0;
    }
    return dst;
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

void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *D = (unsigned char *)dst;
    const unsigned char *S = (const unsigned char *)src;
    size_t i;
    if (D == S || n == 0) {
        return dst;
    }
    if (D < S) {
        for (i = 0; i < n; i++) {
            D[i] = S[i];
        }
    } else {
        for (i = n; i > 0; i--) {
            D[i - 1] = S[i - 1];
        }
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

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *A = (const unsigned char *)a;
    const unsigned char *B = (const unsigned char *)b;
    size_t i;
    for (i = 0; i < n; i++) {
        if (A[i] != B[i]) {
            return A[i] - B[i];
        }
    }
    return 0;
}
