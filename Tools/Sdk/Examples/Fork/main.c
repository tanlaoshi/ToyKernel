/*
 * Examples/Fork — fork + wait（PR-A-examples）
 * 产物：FORK.ELF。wait 不能写成 wait(pid, &status)；退出码用 WEXITSTATUS。
 */
#include <stdio.h>
#include <unistd.h>

int main(void) {
    pid_t Pid = fork();
    int St;

    if (Pid == 0) {
        printf("fork: child pid=%d\n", (int)getpid());
        return 42;
    }
    if (Pid > 0) {
        printf("fork: parent=%d child=%d\n", (int)getpid(), (int)Pid);
        if (wait(&St) < 0) {
            printf("fork: wait failed\n");
            return 1;
        }
        printf("fork: exit=%d\n", WEXITSTATUS(St));
        return 0;
    }
    printf("fork: fork failed\n");
    return 1;
}
