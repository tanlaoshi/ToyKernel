/*
 * libcdemo.c — PR-L2：atoi / qsort / snprintf / signal 冒烟
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

static int CmpInt(const void *a, const void *b) {
    int x = *(const int *)a;
    int y = *(const int *)b;
    return x - y;
}

int main(void) {
    int nums[] = { 3, 1, 4, 1, 5 };
    char buf[64];
    sighandler_t prev;
    int i;

    printf("atoi=%d\n", atoi("  -42"));
    qsort(nums, 5, sizeof(int), CmpInt);
    snprintf(buf, sizeof(buf), "sort=%d%d%d%d%d", nums[0], nums[1], nums[2],
             nums[3], nums[4]);
    printf("%s\n", buf);
    if (strcmp(buf, "sort=11345") != 0) {
        printf("libcdemo: FAIL strcmp\n");
        return 1;
    }

    prev = signal(SIGTERM, SIG_IGN);
    if (prev == SIG_ERR) {
        printf("libcdemo: FAIL signal IGN\n");
        return 1;
    }
    if (signal(SIGTERM, (sighandler_t)2) != SIG_ERR) {
        printf("libcdemo: FAIL custom handler\n");
        return 1;
    }
    (void)i;
    printf("libcdemo: ok\n");
    return 0;
}
