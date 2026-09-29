/*
 * thread_root.c — PR-U-thread-2：CRT 线程入口桩（thr-4 接 create）
 */
#include <toyos/thread.h>
#include <toyos/syscall.h>

void ToyThreadRoot(void *(*Start)(void *), void *Arg) {
    void *Ret = 0;

    if (Start) {
        Ret = Start(Arg);
    }
    /* thr-3：改为 SYS_THREAD_EXIT；现兜底整进程退出 */
    (void)toy_exit((long)Ret);
}
