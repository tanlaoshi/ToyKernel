/*
 * SockDemo.c — 刀 C：POSIX connect(sockaddr) 冒烟（同 NETLIB 路径）
 * Guest：lwip on；宿主 nc -l -p 8888；exec SOCKDEMO.ELF
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <ToyNet.h>

int main(void) {
    int Fd;
    struct sockaddr_in Sa;
    const char *Msg = "from sockdemo!\n";
    char Buf[64];
    ssize_t N;

    printf("sockdemo: ToyNet %s\n", TOY_NET_ABI_VERSION_STRING);

    Fd = socket(AF_INET, SOCK_STREAM, 0);
    if (Fd < 0) {
        printf("sockdemo: socket fail\n");
        return 1;
    }

    memset(&Sa, 0, sizeof(Sa));
    Sa.sin_family = AF_INET;
    Sa.sin_port = htons(8888);
    Sa.sin_addr.s_addr = htonl(ToyNetIpv4(10, 0, 2, 2));

    if (connect(Fd, (struct sockaddr *)&Sa, sizeof(Sa)) != 0) {
        printf("sockdemo: connect fail\n");
        close(Fd);
        return 1;
    }
    if (send(Fd, Msg, strlen(Msg), 0) <= 0) {
        printf("sockdemo: send fail\n");
        close(Fd);
        return 1;
    }
    N = recv(Fd, Buf, sizeof(Buf) - 1, 0);
    close(Fd);
    if (N > 0) {
        Buf[N] = 0;
        printf("sockdemo: ok recv=%s", Buf);
    } else {
        printf("sockdemo: ok\n");
    }
    return 0;
}
