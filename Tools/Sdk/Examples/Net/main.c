/*
 * Examples/Net — libToyNet TCP 客户端（PR-A-net-dns）
 * 产物：MYNET.ELF。Guest 先 lwip on；宿主机 nc -l -p 8888。
 * connect(fd, ip, port) 仍可用；本例走 ToySockAddrIn + ToyNetResolve。
 */
#include <stdio.h>
#include <unistd.h>
#include <ToyNet.h>

int main(void) {
    int Fd;
    ToySockAddrIn Sa;

    printf("net: ToyNet %s\n", TOY_NET_ABI_VERSION_STRING);

    Fd = socket(AF_INET, SOCK_STREAM, 0);
    if (Fd < 0) {
        printf("net: socket fail\n");
        return 1;
    }
    if (ToyNetGetAddrIn(&Sa, "10.0.2.2", 8888) != 0) {
        printf("net: resolve fail\n");
        close(Fd);
        return 1;
    }
    if (ToyNetConnectIn(Fd, &Sa) != 0) {
        printf("net: connect fail\n");
        close(Fd);
        return 1;
    }
    send(Fd, "hi\n", 3, 0);
    close(Fd);
    printf("net: ok\n");
    return 0;
}
