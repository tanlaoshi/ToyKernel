/*
 * cwd.c — getcwd / chdir（SYS_GETCWD / SYS_CHDIR）
 */
#include <stddef.h>
#include <errno.h>
#include <unistd.h>
#include <toyos/syscall.h>

char *getcwd(char *buf, size_t size) {
    if (!buf || size < 2) {
        errno = EINVAL;
        return 0;
    }
    if (toy_syscall(SYS_GETCWD, (long)buf, (long)size, 0) < 0) {
        errno = EINVAL;
        return 0;
    }
    return buf;
}

int chdir(const char *path) {
    if (!path || !path[0]) {
        errno = EINVAL;
        return -1;
    }
    if (toy_syscall(SYS_CHDIR, (long)path, 0, 0) < 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}
