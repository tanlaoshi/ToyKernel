/*
 * killdemo.c — PR-P4：fork 后父进程 kill(SIGTERM)，再 wait 收尸
 */
#include "stdio.h"
#include "unistd.h"
#include "signal.h"

int main(void) {
    pid_t Pid;

    Pid = fork();
    if (Pid < 0) {
        printf("killdemo: fork fail\n");
        return 1;
    }
    if (Pid == 0) {
        /* 子：空转直至被默认终止 */
        for (;;) {
        }
    }

    printf("killdemo: child pid=%d kill SIGTERM\n", (int)Pid);
    if (kill(Pid, SIGTERM) != 0) {
        printf("killdemo: kill fail\n");
        return 1;
    }
    if (wait(0) < 0) {
        printf("killdemo: wait fail\n");
        return 1;
    }
    printf("killdemo: ok\n");
    return 0;
}
