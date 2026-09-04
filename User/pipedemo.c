/*
 * pipedemo.c — PR-P2：pipe + fork；子写父读
 */
#include "stdio.h"
#include "unistd.h"
#include "string.h"

int main(void) {
    int Pfd[2];
    char Buf[16];
    long Pid;
    int N;

    if (pipe(Pfd) != 0) {
        printf("pipedemo: pipe failed\n");
        return 1;
    }
    Pid = fork();
    if (Pid < 0) {
        printf("pipedemo: fork failed\n");
        return 1;
    }
    if (Pid == 0) {
        close(Pfd[0]);
        write(Pfd[1], "PING", 4);
        close(Pfd[1]);
        return 0;
    }
    close(Pfd[1]);
    if (wait(0) < 0) {
        printf("pipedemo: wait failed\n");
        return 1;
    }
    N = (int)read(Pfd[0], Buf, sizeof(Buf) - 1);
    close(Pfd[0]);
    if (N < 0) {
        printf("pipedemo: read failed\n");
        return 1;
    }
    Buf[N] = 0;
    printf("pipedemo: got %s\n", Buf);
    return 0;
}
