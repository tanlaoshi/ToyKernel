/*
 * dirent.c — OpenDirectory / ReadDirectory / FileStat + readdir（刀 B）
 */
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <toyos/syscall.h>

struct ToyDirectory {
    int Fd;
    struct dirent Ent; /* 每 DIR 一份；供 readdir 返回（非进程级静态） */
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
    Dir->Ent.d_name[0] = 0;
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

struct dirent *readdir(DIR *dir) {
    TOY_DIR_ENT Ent;
    int Rc;
    size_t N;

    if (!dir) {
        errno = EINVAL;
        return 0;
    }
    Rc = ReadDirectory(dir, &Ent);
    if (Rc < 0) {
        return 0;
    }
    if (Rc == 0) {
        return 0;
    }
    N = strlen(Ent.Name);
    if (N >= TOY_ENT_NAME_MAX) {
        N = TOY_ENT_NAME_MAX - 1;
    }
    memcpy(dir->Ent.d_name, Ent.Name, N);
    dir->Ent.d_name[N] = 0;
    return &dir->Ent;
}
