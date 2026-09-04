/*
 * printf.c — 最小 printf：%s %d %u %x %c %% → write(1)
 */
#include "stdio.h"
#include "string.h"
#include "toy_syscall.h"

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)

static int PutChar(char C) {
    return (int)toy_write(1, &C, 1);
}

static int PutStr(const char *S) {
    size_t N;
    if (!S) {
        S = "(null)";
    }
    N = strlen(S);
    if (N == 0) {
        return 0;
    }
    return (int)toy_write(1, S, N);
}

static int PutUInt(unsigned long V, unsigned Base, int Upper) {
    char Buf[32];
    int i = 0;
    int n;
    const char *Dig = Upper ? "0123456789ABCDEF" : "0123456789abcdef";

    if (Base < 2 || Base > 16) {
        return 0;
    }
    if (V == 0) {
        return PutChar('0');
    }
    while (V > 0 && i < (int)sizeof(Buf)) {
        Buf[i++] = Dig[V % Base];
        V /= Base;
    }
    n = 0;
    while (i > 0) {
        n += PutChar(Buf[--i]);
    }
    return n;
}

static int PutInt(long V) {
    int n = 0;
    if (V < 0) {
        n += PutChar('-');
        V = -V;
    }
    n += PutUInt((unsigned long)V, 10, 0);
    return n;
}

int puts(const char *s) {
    int n = PutStr(s);
    n += PutChar('\n');
    return n;
}

int printf(const char *fmt, ...) {
    va_list ap;
    int n = 0;
    const char *p;

    if (!fmt) {
        return 0;
    }
    va_start(ap, fmt);
    for (p = fmt; *p; p++) {
        if (*p != '%') {
            n += PutChar(*p);
            continue;
        }
        p++;
        if (!*p) {
            break;
        }
        switch (*p) {
        case '%':
            n += PutChar('%');
            break;
        case 'c':
            n += PutChar((char)va_arg(ap, int));
            break;
        case 's':
            n += PutStr(va_arg(ap, const char *));
            break;
        case 'd':
            n += PutInt((long)va_arg(ap, int));
            break;
        case 'u':
            n += PutUInt((unsigned long)va_arg(ap, unsigned), 10, 0);
            break;
        case 'x':
            n += PutUInt((unsigned long)va_arg(ap, unsigned), 16, 0);
            break;
        case 'X':
            n += PutUInt((unsigned long)va_arg(ap, unsigned), 16, 1);
            break;
        default:
            n += PutChar('%');
            n += PutChar(*p);
            break;
        }
    }
    va_end(ap);
    return n;
}
