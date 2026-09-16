/*
 * Examples/Fork — fork + wait（PR-A-examples）
 * 产物：FORK.ELF。无 getpid；wait 不能写成 wait(pid, &status)。
 */
#include <stdio.h>
#include <unistd.h>

int main(void) {
    pid_t Pid = fork();

    if (Pid == 0) {
        printf("fork: child running\n");
        return 0;
    }
    if (Pid > 0) {
        printf("fork: child pid = %d\n", (int)Pid);
        if (wait(0) < 0) {
            printf("fork: wait failed\n");
            return 1;
        }
        printf("fork: child reaped\n");
        return 0;
    }
    printf("fork: fork failed\n");
    return 1;
}
