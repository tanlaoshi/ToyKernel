/*
 * mmapdemo.c — PR-U-mmap：匿名 mmap 写读 + munmap
 */
#include "stdio.h"
#include "string.h"
#include <sys/mman.h>

#define LEN (8 * 1024)

int main(void) {
    char *P;
    int I;

    P = (char *)mmap(0, LEN, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (P == MAP_FAILED || !P) {
        printf("mmapdemo: mmap fail\n");
        return 1;
    }
    for (I = 0; I < LEN; I++) {
        P[I] = (char)(I & 0xff);
    }
    if (P[0] != 0 || P[LEN - 1] != (char)((LEN - 1) & 0xff)) {
        printf("mmapdemo: corrupt\n");
        return 1;
    }
    if (munmap(P, LEN) != 0) {
        printf("mmapdemo: munmap fail\n");
        return 1;
    }
    printf("mmapdemo: ok %d bytes\n", LEN);
    return 0;
}
