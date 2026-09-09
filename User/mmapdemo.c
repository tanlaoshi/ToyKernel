/*
 * mmapdemo.c — PR-U-mmap 匿名 + PR-U-mmap2 文件私有映射
 */
#include "fcntl.h"
#include "stdio.h"
#include "string.h"
#include "unistd.h"
#include <sys/mman.h>

#define ANON_LEN (8 * 1024)

static int TestAnon(void) {
    char *P;
    int I;

    P = (char *)mmap(0, ANON_LEN, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (P == MAP_FAILED || !P) {
        printf("mmapdemo: anon mmap fail\n");
        return 1;
    }
    for (I = 0; I < ANON_LEN; I++) {
        P[I] = (char)(I & 0xff);
    }
    if (P[0] != 0 || P[ANON_LEN - 1] != (char)((ANON_LEN - 1) & 0xff)) {
        printf("mmapdemo: anon corrupt\n");
        return 1;
    }
    if (munmap(P, ANON_LEN) != 0) {
        printf("mmapdemo: anon munmap fail\n");
        return 1;
    }
    printf("mmapdemo: anon ok %d bytes\n", ANON_LEN);
    return 0;
}

static int TestFile(void) {
    int Fd;
    char Expect[64];
    ssize_t N;
    char *P;
    int I;

    Fd = open("TOYOS.ID", O_RDONLY);
    if (Fd < 0) {
        printf("mmapdemo: open TOYOS.ID fail\n");
        return 1;
    }
    N = read(Fd, Expect, sizeof(Expect) - 1);
    if (N <= 0) {
        printf("mmapdemo: read TOYOS.ID fail\n");
        close(Fd);
        return 1;
    }
    Expect[N] = 0;
    /* 读过后 Pos 前进；映射仍从文件缓冲起点拷贝（教学语义） */
    close(Fd);

    Fd = open("TOYOS.ID", O_RDONLY);
    if (Fd < 0) {
        printf("mmapdemo: reopen fail\n");
        return 1;
    }
    P = (char *)mmap(0, (size_t)N, PROT_READ | PROT_WRITE, MAP_PRIVATE, Fd, 0);
    if (P == MAP_FAILED || !P) {
        printf("mmapdemo: file mmap fail\n");
        close(Fd);
        return 1;
    }
    for (I = 0; I < (int)N; I++) {
        if (P[I] != Expect[I]) {
            printf("mmapdemo: file mismatch at %d\n", I);
            munmap(P, (size_t)N);
            close(Fd);
            return 1;
        }
    }
    /* 私有写：不要求回写文件 */
    P[0] = (char)(P[0] ^ 0x20);
    if (munmap(P, (size_t)N) != 0) {
        printf("mmapdemo: file munmap fail\n");
        close(Fd);
        return 1;
    }
    close(Fd);
    printf("mmapdemo: file ok %d bytes\n", (int)N);
    return 0;
}

int main(void) {
    if (TestAnon() != 0) {
        return 1;
    }
    if (TestFile() != 0) {
        return 1;
    }
    printf("mmapdemo: ok\n");
    return 0;
}
