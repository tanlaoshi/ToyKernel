/*
 * signal.c — PR-L2：signal() 薄封装（无用户 handler 投递）
 */
#include <errno.h>
#include <signal.h>

sighandler_t signal(int sig, sighandler_t handler) {
    if (sig != SIGINT && sig != SIGKILL && sig != SIGTERM) {
        errno = EINVAL;
        return SIG_ERR;
    }
    if (sig == SIGKILL && handler != SIG_DFL) {
        errno = EINVAL;
        return SIG_ERR;
    }
    if (handler != SIG_DFL && handler != SIG_IGN) {
        /* 内核仅默认终止；自定义 handler 留待完整信号 */
        errno = EINVAL;
        return SIG_ERR;
    }
    (void)handler;
    return SIG_DFL;
}
