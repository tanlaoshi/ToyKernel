/*
 * toyos/thread.h — PR-U-thread-2：线程入口约定（子集；非完整 pthread）
 */
#ifndef TOYOS_THREAD_H
#define TOYOS_THREAD_H

#ifndef __ASSEMBLER__

#include <stddef.h>

/*
 * CRT 入口：内核 CreateThread(Rip=ToyThreadRoot, Arg=打包指针) 时使用。
 * Start 返回值暂作 exit 码（thr-3 改 thread_exit）。
 */
void ToyThreadRoot(void *(*Start)(void *), void *Arg);

/* TLS：内核把任务 Id 写在 TlsBase+0；用户可读（x86 %fs:0） */
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
