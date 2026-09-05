/*
 * pkg/net — libToyNet 课外模板（PR-L4）
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ToyNet.h>

int main(void) {
    int Fd = socket(AF_INET, SOCK_STREAM, 0);
    if (Fd < 0) {
        printf("mynet: socket fail\n");
        return 1;
    }
    if (connect(Fd, ToyNetIpv4(10, 0, 2, 2), 8888) != 0) {
        printf("mynet: connect fail\n");
        close(Fd);
        return 1;
    }
    send(Fd, "hi\n", 3, 0);
    close(Fd);
    printf("mynet: ok\n");
    return 0;
}
