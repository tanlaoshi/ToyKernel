/*
 * stat.c — POSIX 形参 stat / fstat（PR-U-stat）
 * stat → FileStat（SYS_FILE_STAT）；fstat → SYS_FSTAT（451）。
 */
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <toyos/syscall.h>

static void FillFromToy(struct stat *St, const TOY_FILE_STAT *T) {
    St->st_mode = (T->Attr & TOY_ATTR_DIR) ? S_IFDIR : S_IFREG;
    St->st_size = (off_t)T->Size;
    St->st_ino  = T->Cluster;
}

int stat(const char *path, struct stat *st) {
    TOY_FILE_STAT T;

    if (!st) {
        errno = EINVAL;
        return -1;
    }
    if (FileStat(path ? path : "", &T) != 0) {
        return -1;
    }
    FillFromToy(st, &T);
    return 0;
}

int fstat(int fd, struct stat *st) {
    TOY_FILE_STAT T;
    long R;

    if (!st) {
        errno = EINVAL;
        return -1;
    }
    R = toy_fstat(fd, &T);
    if (R < 0) {
        errno = EBADF;
        return -1;
    }
    FillFromToy(st, &T);
    return 0;
}
