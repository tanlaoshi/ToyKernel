/*
 * CString.c — freestanding 字符串/内存例程（供 lwIP 等使用）
 */
#include "BootTypes.h"

void *memcpy(void *Dst, const void *Src, UINTN Len) {
    UINT8 *D = (UINT8 *)Dst;
    const UINT8 *S = (const UINT8 *)Src;
    UINTN i;
    for (i = 0; i < Len; i++) {
        D[i] = S[i];
    }
    return Dst;
}

/* GCC _FORTIFY_SOURCE 可能把 memcpy 换成此符号；freestanding 下忽略 destlen */
void *__memcpy_chk(void *Dst, const void *Src, UINTN Len, UINTN DestLen) {
    (void)DestLen;
    return memcpy(Dst, Src, Len);
}

void *memset(void *Dst, int Val, UINTN Len) {
    UINT8 *D = (UINT8 *)Dst;
    UINT8 V = (UINT8)Val;
    UINTN i;
    for (i = 0; i < Len; i++) {
        D[i] = V;
    }
    return Dst;
}

void *__memset_chk(void *Dst, int Val, UINTN Len, UINTN DestLen) {
    (void)DestLen;
    return memset(Dst, Val, Len);
}

void *memmove(void *Dst, const void *Src, UINTN Len) {
    UINT8 *D = (UINT8 *)Dst;
    const UINT8 *S = (const UINT8 *)Src;
    UINTN i;
    if (D == S || Len == 0) {
        return Dst;
    }
    if (D < S) {
        for (i = 0; i < Len; i++) {
            D[i] = S[i];
        }
    } else {
        for (i = Len; i > 0; i--) {
            D[i - 1] = S[i - 1];
        }
    }
    return Dst;
}

int memcmp(const void *A, const void *B, UINTN Len) {
    const UINT8 *P = (const UINT8 *)A;
    const UINT8 *Q = (const UINT8 *)B;
    UINTN i;
    for (i = 0; i < Len; i++) {
        if (P[i] != Q[i]) {
            return (int)P[i] - (int)Q[i];
        }
    }
    return 0;
}

UINTN strlen(const char *S) {
    UINTN N = 0;
    if (S == 0) {
        return 0;
    }
    while (S[N]) {
        N++;
    }
    return N;
}

int strncmp(const char *A, const char *B, UINTN Len) {
    UINTN i;
    for (i = 0; i < Len; i++) {
        if (A[i] != B[i] || A[i] == 0) {
            return (unsigned char)A[i] - (unsigned char)B[i];
        }
    }
    return 0;
}

long strtol(const char *Nptr, char **Endptr, int Base) {
    long Result = 0;
    int Neg = 0;
    const char *P = Nptr;

    if (P == 0) {
        return 0;
    }
    while (*P == ' ' || *P == '\t') {
        P++;
    }
    if (*P == '-') {
        Neg = 1;
        P++;
    } else if (*P == '+') {
        P++;
    }
    if (Base == 0) {
        Base = (*P == '0') ? 16 : 10;
        if (Base == 16 && (P[1] == 'x' || P[1] == 'X')) {
            P += 2;
        }
    }
    for (;; P++) {
        int Digit;
        if (*P >= '0' && *P <= '9') {
            Digit = *P - '0';
        } else if (*P >= 'a' && *P <= 'z') {
            Digit = *P - 'a' + 10;
        } else if (*P >= 'A' && *P <= 'Z') {
            Digit = *P - 'A' + 10;
        } else {
            break;
        }
        if (Digit >= Base) {
            break;
        }
        Result = Result * Base + Digit;
    }
    if (Endptr) {
        *Endptr = (char *)P;
    }
    return Neg ? -Result : Result;
}

unsigned short **__ctype_b_loc(void) {
    static unsigned short Table[256];
    static unsigned short *Ptr = Table;
    static int Init;
    int i;
    if (!Init) {
        for (i = 0; i < 256; i++) {
            Table[i] = (unsigned short)((i >= '0' && i <= '9') ? 0x800 : 0);
        }
        Init = 1;
    }
    return &Ptr;
}
