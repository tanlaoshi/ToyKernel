/*
 * printf.c — printf / sprintf / snprintf（%s %d %u %x %c %%）（PR-L2）
 */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <toyos/syscall.h>

typedef struct {
    char *Buf;
    size_t Cap;   /* snprintf：含 NUL 的容量；0=无限（sprintf） */
    size_t Pos;   /* 已写入字符数（不含 NUL；可超过 Cap-1） */
    int ToFd;     /* 1=write(1)；0=写 Buf */
} FmtOut;

static void OutChar(FmtOut *O, char C) {
    if (O->ToFd) {
        toy_write(1, &C, 1);
        O->Pos++;
        return;
    }
    if (O->Cap == 0 || O->Pos + 1 < O->Cap) {
        if (O->Buf) {
            O->Buf[O->Pos] = C;
        }
    }
    O->Pos++;
}

static void OutStr(FmtOut *O, const char *S) {
    if (!S) {
        S = "(null)";
    }
    while (*S) {
        OutChar(O, *S++);
    }
}

static void OutUInt(FmtOut *O, unsigned long V, unsigned Base, int Upper) {
    char Buf[32];
    int i = 0;
    const char *Dig = Upper ? "0123456789ABCDEF" : "0123456789abcdef";

    if (Base < 2 || Base > 16) {
        return;
    }
    if (V == 0) {
        OutChar(O, '0');
        return;
    }
    while (V > 0 && i < (int)sizeof(Buf)) {
        Buf[i++] = Dig[V % Base];
        V /= Base;
    }
    while (i > 0) {
        OutChar(O, Buf[--i]);
    }
}

static void OutInt(FmtOut *O, long V) {
    if (V < 0) {
        OutChar(O, '-');
        V = -V;
    }
    OutUInt(O, (unsigned long)V, 10, 0);
}

static int Format(FmtOut *O, const char *fmt, va_list ap) {
    const char *p;

    if (!fmt) {
        return 0;
    }
    for (p = fmt; *p; p++) {
        if (*p != '%') {
            OutChar(O, *p);
            continue;
        }
        p++;
        if (!*p) {
            break;
        }
        switch (*p) {
        case '%':
            OutChar(O, '%');
            break;
        case 'c':
            OutChar(O, (char)va_arg(ap, int));
            break;
        case 's':
            OutStr(O, va_arg(ap, const char *));
            break;
        case 'd':
            OutInt(O, (long)va_arg(ap, int));
            break;
        case 'u':
            OutUInt(O, (unsigned long)va_arg(ap, unsigned), 10, 0);
            break;
        case 'x':
            OutUInt(O, (unsigned long)va_arg(ap, unsigned), 16, 0);
            break;
        case 'X':
            OutUInt(O, (unsigned long)va_arg(ap, unsigned), 16, 1);
            break;
        default:
            OutChar(O, '%');
            OutChar(O, *p);
            break;
        }
    }
    return (int)O->Pos;
}

int vprintf(const char *fmt, va_list ap) {
    FmtOut O;
    O.Buf = 0;
    O.Cap = 0;
    O.Pos = 0;
    O.ToFd = 1;
    return Format(&O, fmt, ap);
}

int printf(const char *fmt, ...) {
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    FmtOut O;
    int n;
    O.Buf = buf;
    O.Cap = size; /* 0 = 不截断（vsprintf） */
    O.Pos = 0;
    O.ToFd = 0;
    n = Format(&O, fmt, ap);
    if (buf) {
        if (size == 0) {
            buf[O.Pos] = 0;
        } else {
            size_t end = (O.Pos < size) ? O.Pos : size - 1;
            buf[end] = 0;
        }
    }
    return n;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

int vsprintf(char *buf, const char *fmt, va_list ap) {
    return vsnprintf(buf, 0, fmt, ap);
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsprintf(buf, fmt, ap);
    va_end(ap);
    return n;
}

int puts(const char *s) {
    FmtOut O;
    va_list unused;
    (void)unused;
    O.Buf = 0;
    O.Cap = 0;
    O.Pos = 0;
    O.ToFd = 1;
    OutStr(&O, s);
    OutChar(&O, '\n');
    return (int)O.Pos;
}

void perror(const char *s) {
    FmtOut O;
    O.Buf = 0;
    O.Cap = 0;
    O.Pos = 0;
    O.ToFd = 1;
    if (s && s[0]) {
        OutStr(&O, s);
        OutStr(&O, ": ");
    }
    OutStr(&O, "errno=");
    OutInt(&O, (long)errno);
    OutChar(&O, '\n');
}
