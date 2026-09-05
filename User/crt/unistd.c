/*
 * unistd.c — open/read/write/close，失败置 errno（PR-CRT2）
 */
#include "errno.h"
#include "fcntl.h"
#include "unistd.h"
#include "toy_syscall.h"

int open(const char *path, int flags) {
    long r;

    (void)flags;
    if (!path || !path[0]) {
        errno = EINVAL;
        return -1;
    }
    r = toy_open(path);
    if (r < 0) {
        errno = ENOENT;
        return -1;
    }
    return (int)r;
}

ssize_t read(int fd, void *buf, size_t count) {
    long r;

    if (fd < 0 || !buf) {
        errno = EINVAL;
        return -1;
    }
    r = (long)toy_read(fd, buf, count);
    if (r < 0) {
        errno = EIO;
        return -1;
    }
    return (ssize_t)r;
}

ssize_t write(int fd, const void *buf, size_t count) {
    long r;

    if (fd < 0 || (!buf && count > 0)) {
        errno = EINVAL;
        return -1;
    }
    r = (long)toy_write(fd, buf, count);
    if (r < 0) {
        errno = EIO;
        return -1;
    }
    return (ssize_t)r;
}

int close(int fd) {
    long r;

    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    r = toy_close(fd);
    if (r < 0) {
        errno = EBADF;
        return -1;
    }
    return 0;
}

int execve(const char *path, char *const argv[], char *const envp[]) {
    long r;

    if (!path || !path[0]) {
        errno = EINVAL;
        return -1;
    }
    r = toy_execve(path, argv, envp);
    /* 成功则映像已替换，不会回到这里 */
    errno = (r < 0) ? ENOENT : EIO;
    return -1;
}

int pipe(int pipefd[2]) {
    long r;

    if (!pipefd) {
        errno = EINVAL;
        return -1;
    }
    r = toy_pipe(pipefd);
    if (r < 0) {
        errno = EMFILE;
        return -1;
    }
    return 0;
}

int dup(int fd) {
    long r;

    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    r = toy_dup(fd);
    if (r < 0) {
        errno = EBADF;
        return -1;
    }
    return (int)r;
}

pid_t fork(void) {
    long r = toy_fork();
    if (r < 0) {
        errno = EAGAIN;
        return -1;
    }
    return (pid_t)r;
}

pid_t wait(int *status) {
    long r = toy_wait(0);
    if (r < 0) {
        errno = ECHILD;
        return -1;
    }
    if (status) {
        *status = 0;
    }
    return (pid_t)r;
}

void *brk(void *addr) {
    long r = toy_brk((long)addr);
    if (r < 0) {
        errno = ENOMEM;
        return (void *)(long)-1;
    }
    return (void *)r;
}

int kill(pid_t pid, int sig) {
    long r;

    if (pid <= 0 || (sig != SIGKILL && sig != SIGTERM && sig != SIGINT)) {
        errno = EINVAL;
        return -1;
    }
    r = toy_kill(pid, sig);
    if (r < 0) {
        errno = ESRCH;
        return -1;
    }
    return 0;
}

int create_window(const char *title, unsigned w, unsigned h) {
    long r;

    if (!title || w == 0 || h == 0) {
        errno = EINVAL;
        return -1;
    }
    r = toy_create_window(title, (long)w, (long)h);
    if (r < 0) {
        errno = ENOMEM;
        return -1;
    }
    return (int)r;
}

int damage(int wid, const char *text) {
    long r;

    if (wid < 0 || !text) {
        errno = EINVAL;
        return -1;
    }
    r = toy_damage(wid, text);
    if (r < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int poll_input(int wid) {
    long r;

    if (wid < 0) {
        errno = EINVAL;
        return -1;
    }
    r = toy_poll_input(wid);
    if (r < 0) {
        errno = EINVAL;
        return -1;
    }
    return (int)r;
}
