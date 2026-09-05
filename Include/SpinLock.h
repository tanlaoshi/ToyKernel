/*
 * SpinLock.h — 架构无关自旋锁（PR-S3；PR-A5 用 HalCpuRelax）
 *
 * 获取时关中断，避免「持锁时被定时器打断再抢同一把锁」死锁
 *（Halt 保持 IF=1 后，Shell exec→CreateUser 会踩中）。
 * 空转等待期间短暂开中断，避免长时间关中断。
 */
#ifndef SPIN_LOCK_H
#define SPIN_LOCK_H

#include "BootTypes.h"
#include "Hal.h"

typedef struct {
    volatile UINT32 Locked;
    UINT64          IrqFlags; /* 持有者私有；持锁期间有效 */
} SPIN_LOCK;

static inline void SpinLockInit(SPIN_LOCK *Lock) {
    if (Lock) {
        Lock->Locked = 0;
        Lock->IrqFlags = 0;
    }
}

static inline void SpinLockAcquire(SPIN_LOCK *Lock) {
    UINT64 Flags;

    if (!Lock) {
        return;
    }
    Flags = HalIrqSave();
    while (__sync_lock_test_and_set(&Lock->Locked, 1u)) {
        HalIrqRestore(Flags);
        HalCpuRelax();
        Flags = HalIrqSave();
    }
    Lock->IrqFlags = Flags;
}

static inline void SpinLockRelease(SPIN_LOCK *Lock) {
    UINT64 Flags;

    if (!Lock) {
        return;
    }
    Flags = Lock->IrqFlags;
    __sync_lock_release(&Lock->Locked);
    HalIrqRestore(Flags);
}

#endif
