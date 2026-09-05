/*
 * netlibdemo.c — PR-L4：用 libToyNet 重写 NETDEMO 客户端路径
 * 需：./build.sh LWIP=1，Shell `lwip on`，宿主机 nc -l -p 8888
 * Shell：exec NETLIB.ELF
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ToyNet.h>

int main(void) {
    int Fd;
    char Buf[64];
    ssize_t N;
    const char *Msg = "from libnet!\n";

    printf("netlib: ToyNet %s\n", TOY_NET_ABI_VERSION_STRING);

    Fd = socket(AF_INET, SOCK_STREAM, 0);
    if (Fd < 0) {
        printf("netlib: socket fail (need LWIP=1?)\n");
        return 1;
    }
    if (connect(Fd, ToyNetIpv4(10, 0, 2, 2), 8888) != 0) {
        printf("netlib: connect fail\n");
        close(Fd);
        return 1;
    }
    if (send(Fd, Msg, strlen(Msg), 0) <= 0) {
        printf("netlib: send fail\n");
        close(Fd);
        return 1;
    }
    N = recv(Fd, Buf, sizeof(Buf) - 1, 0);
    close(Fd);
    if (N > 0) {
        Buf[N] = 0;
        printf("netlib: ok recv=%s", Buf);
    } else {
        printf("netlib: ok\n");
    }
    return 0;
}
