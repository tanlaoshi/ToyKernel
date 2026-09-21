/*
 * SleepDemo.c — sleep / clock_ms 稳节拍冒烟
 *
 * 验收：exec SLEEPDEMO → msleep/sleep 间隔合理 → sleepdemo: ok
 * CRT printf 仅 %s %d %u %x %c（无 %lu）
 */
#include <stdio.h>
#include <unistd.h>

int main(void) {
    unsigned T0;
    unsigned T1;
    unsigned Dt;
    int i;

    printf("sleepdemo: start clock=%u\n", (unsigned)clock_ms());

    for (i = 0; i < 3; i++) {
        T0 = (unsigned)clock_ms();
        msleep(200);
        T1 = (unsigned)clock_ms();
        Dt = (T1 >= T0) ? (T1 - T0) : 0;
        printf("sleepdemo: msleep(200) dt=%u\n", Dt);
        if (Dt < 150 || Dt > 800) {
            printf("sleepdemo: FAIL dt=%u\n", Dt);
            return 1;
        }
    }

    T0 = (unsigned)clock_ms();
    sleep(1);
    T1 = (unsigned)clock_ms();
    Dt = (T1 >= T0) ? (T1 - T0) : 0;
    printf("sleepdemo: sleep(1) dt=%u\n", Dt);
    if (Dt < 700 || Dt > 2000) {
        printf("sleepdemo: FAIL sleep(1) dt=%u\n", Dt);
        return 1;
    }

    printf("sleepdemo: ok\n");
    return 0;
}
