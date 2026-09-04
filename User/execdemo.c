/*
 * execdemo.c — PR-P1：用户态 execve 再加载 HELLO.ELF
 */
#include "stdio.h"
#include "unistd.h"

int main(int argc, char **argv) {
    char *Av[2];

    (void)argc;
    (void)argv;
    printf("execdemo: calling execve HELLO.ELF\n");
    Av[0] = "HELLO.ELF";
    Av[1] = 0;
    execve("HELLO.ELF", Av, 0);
    printf("execdemo: execve failed\n");
    return 1;
}
