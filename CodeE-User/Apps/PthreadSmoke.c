/*
 * PthreadSmoke.c — thr-4：pthread_create/join + 自旋 mutex 冒烟
 */
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <toyos/thread.h>

#define LOOPS 2000

static pthread_mutex_t GLock = PTHREAD_MUTEX_INITIALIZER;
static volatile int GCount;

static void *Worker(void *Arg) {
    int N = (int)(long)Arg;
    int i;

    for (i = 0; i < LOOPS; i++) {
        pthread_mutex_lock(&GLock);
        GCount++;
        pthread_mutex_unlock(&GLock);
    }
    printf("pthread: worker tid=%d n=%d\n", (int)toy_gettid(), N);
    return (void *)(long)(N + 1);
}

int main(void) {
    pthread_t A;
    pthread_t B;
    void *Ra = 0;
    void *Rb = 0;

    printf("pthreadsmoke: pid=%d tid=%d\n", (int)getpid(), (int)toy_gettid());
    GCount = 0;
    if (pthread_create(&A, 0, Worker, (void *)(long)1) != 0 ||
        pthread_create(&B, 0, Worker, (void *)(long)2) != 0) {
        printf("pthreadsmoke: create fail\n");
        return 1;
    }
    if (pthread_join(A, &Ra) != 0 || pthread_join(B, &Rb) != 0) {
        printf("pthreadsmoke: join fail\n");
        return 1;
    }
    printf("pthreadsmoke: ra=%d rb=%d count=%d expect=%d\n", (int)(long)Ra,
           (int)(long)Rb, GCount, LOOPS * 2);
    if ((long)Ra != 2 || (long)Rb != 3 || GCount != LOOPS * 2) {
        printf("pthreadsmoke: FAIL\n");
        return 1;
    }
    printf("pthreadsmoke: ok\n");
    return 0;
}
