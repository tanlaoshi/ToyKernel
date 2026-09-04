/*
 * writefile.c — CRT2：写 USERNOTE.TXT（WRITE.ELF）
 * 旧 int 0x80 汇编见 writefile.S
 */
#include "errno.h"
#include "fcntl.h"
#include "stdio.h"
#include "unistd.h"

int main(void) {
    static const char msg[] = "from ring3!\n";
    int fd;
    ssize_t n;

    fd = open("USERNOTE.TXT", O_WRONLY | O_CREAT);
    if (fd < 0) {
        printf("write: open failed errno=%d\n", errno);
        return 1;
    }
    n = write(fd, msg, sizeof(msg) - 1);
    if (n < 0) {
        printf("write: write failed errno=%d\n", errno);
        close(fd);
        return 1;
    }
    if (close(fd) < 0) {
        printf("write: close failed errno=%d\n", errno);
        return 1;
    }
    printf("ok\n");
    return 0;
}
