/*
 * sys/stat.h — 教学最小 stat / fstat（PR-U-stat）
 * 非完整 POSIX struct stat；st_mode 仅 S_IFDIR / S_IFREG。
 */
#ifndef SYS_STAT_H
#define SYS_STAT_H

#include <sys/types.h>

#define S_IFDIR 0x10
#define S_IFREG 0x80

#define S_ISDIR(m) (((m) & S_IFDIR) != 0)
#define S_ISREG(m) (((m) & S_IFREG) != 0)

struct stat {
    mode_t   st_mode;
    off_t    st_size;
    unsigned st_ino;
};

int stat(const char *path, struct stat *st);
int fstat(int fd, struct stat *st);

#endif
