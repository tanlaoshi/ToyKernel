/*
 * pthread.c — thr-4：pthread_* 薄封装 + 自旋 mutex
 */
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <toyos/thread.h>

int pthread_create(pthread_t *Thread, const pthread_attr_t *Attr,
                   void *(*Start)(void *), void *Arg) {
    long Tid;

    (void)Attr;
    if (!Thread || !Start) {
        errno = EINVAL;
        return EINVAL;
    }
    /* 经 ToyThreadRoot：Start 返回后自动 thread_exit */
    Tid = toy_syscall(SYS_THREAD_CREATE, (long)ToyThreadRoot, (long)Start,
                      (long)Arg);
    if (Tid < 0) {
        errno = (int)(-Tid);
        return errno;
    }
    *Thread = (pthread_t)Tid;
    return 0;
}

int pthread_join(pthread_t Thread, void **Retval) {
    int St = 0;
    long R;

    R = toy_thread_join((long)Thread, &St);
    if (R != 0) {
        errno = (int)(-R);
        return errno;
    }
    if (Retval) {
        *Retval = (void *)(long)St;
    }
    return 0;
}

void pthread_exit(void *Retval) {
    (void)toy_thread_exit((long)Retval);
    for (;;) {
    }
}

int pthread_mutex_init(pthread_mutex_t *M, const pthread_mutexattr_t *Attr) {
    (void)Attr;
    if (!M) {
        errno = EINVAL;
        return EINVAL;
    }
    M->Lock = 0;
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *M) {
    if (!M) {
        errno = EINVAL;
        return EINVAL;
    }
    M->Lock = 0;
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *M) {
    if (!M) {
        errno = EINVAL;
        return EINVAL;
    }
    if (__sync_lock_test_and_set(&M->Lock, 1) != 0) {
        errno = EAGAIN;
        return EAGAIN;
    }
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *M) {
    if (!M) {
        errno = EINVAL;
        return EINVAL;
    }
    while (__sync_lock_test_and_set(&M->Lock, 1) != 0) {
        (void)sched_yield();
    }
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *M) {
    if (!M) {
        errno = EINVAL;
        return EINVAL;
    }
    __sync_lock_release(&M->Lock);
    return 0;
}
