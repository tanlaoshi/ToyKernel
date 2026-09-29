/*
 * toyos/thread.h — PR-U-thread：入口桩 + create/join/exit/gettid
 */
#ifndef TOYOS_THREAD_H
#define TOYOS_THREAD_H

#ifndef __ASSEMBLER__

#include <stddef.h>
#include <toyos/syscall.h>

void ToyThreadRoot(void *(*Start)(void *), void *Arg);

static inline long toy_thread_create(void *(*Start)(void *), void *Arg) {
    /* entry=ToyThreadRoot；Arg0=Start，Arg1=Arg（thr-4） */
    return toy_syscall(SYS_THREAD_CREATE, (long)ToyThreadRoot, (long)Start,
                       (long)Arg);
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
