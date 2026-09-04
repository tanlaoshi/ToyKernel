/*
 * cat.c — CRT2：open/read/write/close 读 TOYOS.ID（CAT.ELF）
 * 旧 int 0x80 汇编见 catfile.S
 */
#include "errno.h"
#include "fcntl.h"
#include "stdio.h"
#include "unistd.h"

int main(void) {
    char buf[64];
    int fd;
    ssize_t n;

    fd = open("TOYOS.ID", O_RDONLY);
    if (fd < 0) {
        printf("cat: open failed errno=%d\n", errno);
        return 1;
    }
    for (;;) {
        n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            printf("cat: read failed errno=%d\n", errno);
            close(fd);
            return 1;
        }
        if (n == 0) {
            break;
        }
        if (write(1, buf, (size_t)n) < 0) {
            printf("cat: write failed errno=%d\n", errno);
            close(fd);
            return 1;
        }
    }
    close(fd);
    return 0;
}
