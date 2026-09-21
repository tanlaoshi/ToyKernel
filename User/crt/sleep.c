/*
 * sleep.c — sleep / usleep / msleep / clock_ms（SYS_SLEEP / SYS_CLOCK_MS）
 */
#include <unistd.h>
#include <toyos/syscall.h>

unsigned long clock_ms(void) {
    return (unsigned long)toy_clock_ms();
}

unsigned msleep(unsigned ms) {
    (void)toy_sleep((long)ms);
    return 0;
}

unsigned sleep(unsigned seconds) {
    unsigned long Ms;

    if (seconds == 0) {
        return 0;
    }
    if (seconds > 60u) {
        seconds = 60u;
    }
    Ms = (unsigned long)seconds * 1000ul;
    (void)toy_sleep((long)Ms);
    return 0;
}

int usleep(unsigned usec) {
    unsigned Ms;

    if (usec == 0) {
        return 0;
    }
    Ms = (usec + 999u) / 1000u;
    if (Ms == 0) {
        Ms = 1;
    }
    (void)toy_sleep((long)Ms);
    return 0;
}
