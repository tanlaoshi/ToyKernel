/*
 * Examples/File — open / write / read（PR-A-examples）
 * 产物：FILEIO.ELF；flags 宏可写，内核目前忽略。
 */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

int main(void) {
    int Fd;
    const char *Msg = "Hello from app!\n";
    char Buf[128];
    int N;

    Fd = open("TOYOS:NOTE.TXT", O_WRONLY | O_CREAT);
    if (Fd < 0) {
        printf("fileio: open write failed\n");
        return 1;
    }
    write(Fd, Msg, strlen(Msg));
    close(Fd);

    Fd = open("TOYOS:NOTE.TXT", O_RDONLY);
    if (Fd < 0) {
        printf("fileio: open read failed\n");
        return 1;
    }
    N = (int)read(Fd, Buf, sizeof(Buf) - 1);
    close(Fd);
    if (N > 0) {
        Buf[N] = 0;
        printf("fileio: %s", Buf);
    }
    return 0;
}
