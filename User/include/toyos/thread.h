/*
 * toyos/thread.h — PR-U-thread：线程入口 + thr-3 syscall 薄封装
 */
#ifndef TOYOS_THREAD_H
#define TOYOS_THREAD_H

#ifndef __ASSEMBLER__

#include <stddef.h>
#include <toyos/syscall.h>

void ToyThreadRoot(void *(*Start)(void *), void *Arg);

static inline long toy_thread_create(void *(*Start)(void *), void *Arg) {
    /*
     * 内核入口 = ToyThreadRoot；Arg0=Start，Arg1=Arg。
     * 帧约定：CreateThread 把 Arg 写入首参寄存器 = Start；
     * 第二参需另约定——简化：Start 与 Arg 打成栈上结构由用户自备。
     * thr-3 最小：entry 直接为 Start，arg 为 Arg（不经 ToyThreadRoot）。
     */
    return toy_syscall(SYS_THREAD_CREATE, (long)Start, (long)Arg, 0);
}

static inline long toy_thread_join(long tid, int *status) {
    TOY_RET2 R = toy_syscall2(SYS_THREAD_JOIN, tid, (long)status, 0);
    if (status && R.A == 0) {
        *status = (int)R.B;
    }
    return R.A;
}

static inline long toy_thread_exit(long code) {
    return toy_syscall(SYS_THREAD_EXIT, code, 0, 0);
}

static inline long toy_gettid(void) {
    return toy_syscall(SYS_GETTID, 0, 0, 0);
}

static inline unsigned long toy_tls_tid(void) {
#if defined(__x86_64__)
    unsigned long V;
    __asm__ volatile("mov %%fs:0, %0" : "=r"(V));
    return V;
#elif defined(__aarch64__)
    unsigned long V;
    __asm__ volatile("mrs %0, tpidr_el0" : "=r"(V));
    return *(unsigned long *)V;
#elif defined(__riscv)
    unsigned long V;
    __asm__ volatile("mv %0, tp" : "=r"(V));
    return *(unsigned long *)V;
#else
    return 0;
#endif
}

#endif /* !__ASSEMBLER__ */
#endif /* TOYOS_THREAD_H */
