/*
 * dirent.c — OpenDirectory / ReadDirectory / FileStat（PR-F4）
 */
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <toyos/syscall.h>

struct ToyDirectory {
    int Fd;
};

TOY_DIR *OpenDirectory(const char *path) {
    TOY_DIR *Dir;
    long Fd;

    Dir = (TOY_DIR *)malloc(sizeof(*Dir));
    if (!Dir) {
        errno = ENOMEM;
        return 0;
    }
    Fd = toy_open_directory(path ? path : "");
    if (Fd < 0) {
        free(Dir);
        errno = ENOTDIR;
        return 0;
    }
    Dir->Fd = (int)Fd;
    return Dir;
}

int ReadDirectory(TOY_DIR *dir, TOY_DIR_ENT *out) {
    long R;

    if (!dir || !out) {
        errno = EINVAL;
        return -1;
    }
    R = toy_read_directory(dir->Fd, out);
    if (R < 0) {
        errno = EBADF;
        return -1;
    }
    return (int)R;
}

int CloseDirectory(TOY_DIR *dir) {
    long R;

    if (!dir) {
        errno = EINVAL;
        return -1;
    }
    R = toy_close(dir->Fd);
    free(dir);
    if (R < 0) {
        errno = EBADF;
        return -1;
    }
    return 0;
}

int FileStat(const char *path, TOY_FILE_STAT *out) {
    long R;

    if (!out) {
        errno = EINVAL;
        return -1;
    }
    R = toy_file_stat(path ? path : "", out);
    if (R < 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}
