/*
 * SpinLock.h — 架构无关自旋锁（PR-S3）
 *
 * 持锁时间须短；勿在持锁时长时间关中断空转。
 * x86 用 lock xchg；其它架构同样用 __sync_*（GCC 内建）。
 */
#ifndef SPIN_LOCK_H
#define SPIN_LOCK_H

#include "BootTypes.h"

typedef struct {
    volatile UINT32 Locked;
} SPIN_LOCK;

static inline void SpinLockInit(SPIN_LOCK *Lock) {
    if (Lock) {
        Lock->Locked = 0;
    }
}

static inline void SpinLockAcquire(SPIN_LOCK *Lock) {
    if (!Lock) {
        return;
    }
    while (__sync_lock_test_and_set(&Lock->Locked, 1u)) {
        __asm__ volatile ("pause" ::: "memory");
    }
}

static inline void SpinLockRelease(SPIN_LOCK *Lock) {
    if (!Lock) {
        return;
    }
    __sync_lock_release(&Lock->Locked);
}

#endif
