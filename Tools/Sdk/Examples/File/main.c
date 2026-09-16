/*
 * Examples/File — open / write / read + fopen / fseek（PR-A-libc）
 * 产物：FILEIO.ELF；flags 宏可写，内核目前忽略；fopen("w") 不截断。
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
    FILE *Fp;
    size_t Got;

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
    if (lseek(Fd, 6, SEEK_SET) != 6) {
        printf("fileio: lseek failed\n");
        close(Fd);
        return 1;
    }
    N = (int)read(Fd, Buf, sizeof(Buf) - 1);
    close(Fd);
    if (N > 0) {
        Buf[N] = 0;
        printf("fileio: %s", Buf);
    }

    Fp = fopen("TOYOS:NOTE.TXT", "r");
    if (!Fp) {
        printf("fileio: fopen failed\n");
        return 1;
    }
    if (fseek(Fp, 0, SEEK_SET) != 0) {
        printf("fileio: fseek failed\n");
        fclose(Fp);
        return 1;
    }
    Got = fread(Buf, 1, sizeof(Buf) - 1, Fp);
    fclose(Fp);
    if (Got > 0) {
        Buf[Got] = 0;
        printf("fileio fopen: %s", Buf);
    }
    return 0;
}
