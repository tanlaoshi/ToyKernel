/*
 * signal.c — PR-U-sig：signal() → SYS_SIGNAL（教学级用户 handler）
 */
#include <errno.h>
#include <signal.h>
#include <toyos/syscall.h>

sighandler_t signal(int sig, sighandler_t handler) {
    long r;

    if (sig != SIGINT && sig != SIGKILL && sig != SIGTERM) {
        errno = EINVAL;
        return SIG_ERR;
    }
    if (sig == SIGKILL && handler != SIG_DFL) {
        errno = EINVAL;
        return SIG_ERR;
    }
    r = toy_signal((long)sig, (long)handler);
    if (r < 0) {
        errno = EINVAL;
        return SIG_ERR;
    }
    return (sighandler_t)r;
}
