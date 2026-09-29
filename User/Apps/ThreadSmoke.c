/*
 * ThreadSmoke.c — thr-3：create / join / gettid 冒烟
 */
#include <stdio.h>
#include <unistd.h>
#include <toyos/thread.h>

static volatile int GFlag;

static void *Worker(void *Arg) {
    long N = (long)Arg;
    printf("worker: tid=%d arg=%d\n", (int)toy_gettid(), (int)N);
    GFlag = (int)N + 1;
    toy_thread_exit(42);
    return 0;
}

int main(void) {
    long Tid;
    int St = 0;

    printf("threadsmoke: pid=%d tid=%d\n", (int)getpid(), (int)toy_gettid());
    GFlag = 0;
    Tid = toy_thread_create(Worker, (void *)(long)7);
    if (Tid < 0) {
        printf("threadsmoke: create fail %d\n", (int)Tid);
        return 1;
    }
    printf("threadsmoke: created tid=%d\n", (int)Tid);
    if (toy_thread_join(Tid, &St) != 0) {
        printf("threadsmoke: join fail\n");
        return 1;
    }
    printf("threadsmoke: joined st=%d flag=%d\n", St, GFlag);
    if (St != 42 || GFlag != 8) {
        printf("threadsmoke: FAIL\n");
        return 1;
    }
    printf("threadsmoke: ok\n");
    return 0;
}
