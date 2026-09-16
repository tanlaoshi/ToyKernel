/*
 * Examples/Pipe — pipe + fork；子写父读（PR-A-examples）
 * 产物：PIPE.ELF。wait 是 wait(int *)，父进程 wait(0)。
 */
#include <stdio.h>
#include <unistd.h>

int main(void) {
    int Pfd[2];
    char Buf[16];
    pid_t Pid;
    int N;

    if (pipe(Pfd) != 0) {
        printf("pipe: pipe failed\n");
        return 1;
    }
    Pid = fork();
    if (Pid < 0) {
        printf("pipe: fork failed\n");
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
        printf("pipe: wait failed\n");
        return 1;
    }
    N = (int)read(Pfd[0], Buf, sizeof(Buf) - 1);
    close(Pfd[0]);
    if (N < 0) {
        printf("pipe: read failed\n");
        return 1;
    }
    Buf[N] = 0;
    printf("pipe: got %s\n", Buf);
    return 0;
}
