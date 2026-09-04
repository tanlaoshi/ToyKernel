/*
 * brkdemo.c — PR-P3：malloc 超过旧 BSS 8KiB 上限仍成功
 */
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

#define BIG (16 * 1024)

int main(void) {
    char *P;
    int I;

    P = (char *)malloc(BIG);
    if (!P) {
        printf("brkdemo: malloc %d fail\n", BIG);
        return 1;
    }
    for (I = 0; I < BIG; I++) {
        P[I] = (char)(I & 0xff);
    }
    if (P[0] != 0 || P[BIG - 1] != (char)((BIG - 1) & 0xff)) {
        printf("brkdemo: corrupt\n");
        return 1;
    }
    printf("brkdemo: ok %d bytes\n", BIG);
    return 0;
}
