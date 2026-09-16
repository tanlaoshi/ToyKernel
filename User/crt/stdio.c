/*
 * stdio.c — PR-A-libc：无缓冲 fopen / fread / fwrite / fseek
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>

#define FF_READ  1u
#define FF_WRITE 2u

struct ToyFile {
    int fd;
    unsigned flags;
    int eof;
    int err;
};

static int ParseMode(const char *mode, unsigned *OutFlags, int *OutAppend, int *OutExist) {
    unsigned Flags = 0;
    int Append = 0;
    int Plus = 0;
    char Primary = 0;
    const char *P;

    if (!mode || !mode[0] || !OutFlags || !OutAppend || !OutExist) {
        return -1;
    }
    for (P = mode; *P; P++) {
        if (*P == 'b') {
            continue;
        }
        if (*P == '+') {
            Plus = 1;
            continue;
        }
        if (*P == 'r' || *P == 'w' || *P == 'a') {
            if (Primary) {
                return -1;
            }
            Primary = *P;
            continue;
        }
        return -1;
    }
    if (!Primary) {
        return -1;
    }
    if (Primary == 'r') {
        Flags = FF_READ;
    } else if (Primary == 'w') {
        Flags = FF_WRITE;
    } else {
        Flags = FF_WRITE;
        Append = 1;
    }
    if (Plus) {
        Flags = FF_READ | FF_WRITE;
    }
    *OutFlags = Flags;
    *OutAppend = Append;
    *OutExist = (Primary == 'r');
    return 0;
}

FILE *fopen(const char *path, const char *mode) {
    unsigned Flags;
    int Append;
    int MustExist;
    int Fd;
    FILE *Fp;
    TOY_FILE_STAT St;

    if (ParseMode(mode, &Flags, &Append, &MustExist) < 0 || !path || !path[0]) {
        errno = EINVAL;
        return 0;
    }
    if (MustExist) {
        if (FileStat(path, &St) < 0) {
            errno = ENOENT;
            return 0;
        }
        if (St.Attr & TOY_ATTR_DIR) {
            errno = EINVAL;
            return 0;
        }
    }
    Fd = open(path, (Flags & FF_WRITE) ? (O_RDWR | O_CREAT) : O_RDONLY);
    if (Fd < 0) {
        return 0;
    }
    if (Append) {
        if (lseek(Fd, 0, SEEK_END) < 0) {
            close(Fd);
            return 0;
        }
    }
    Fp = (FILE *)malloc(sizeof(*Fp));
    if (!Fp) {
        close(Fd);
        errno = ENOMEM;
        return 0;
    }
    Fp->fd = Fd;
    Fp->flags = Flags;
    Fp->eof = 0;
    Fp->err = 0;
    return Fp;
}

int fclose(FILE *fp) {
    int Rc;

    if (!fp) {
        errno = EINVAL;
        return EOF;
    }
    Rc = close(fp->fd);
    free(fp);
    return Rc;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *fp) {
    size_t Bytes;
    ssize_t N;

    if (!fp || !ptr || size == 0 || nmemb == 0) {
        return 0;
    }
    if ((fp->flags & FF_READ) == 0) {
        fp->err = 1;
        errno = EINVAL;
        return 0;
    }
    if (size != 0 && nmemb > ((size_t)-1) / size) {
        fp->err = 1;
        errno = EINVAL;
        return 0;
    }
    Bytes = size * nmemb;
    N = read(fp->fd, ptr, Bytes);
    if (N < 0) {
        fp->err = 1;
        return 0;
    }
    if ((size_t)N < Bytes) {
        fp->eof = 1;
    }
    return (size_t)N / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *fp) {
    size_t Bytes;
    ssize_t N;

    if (!fp || !ptr || size == 0 || nmemb == 0) {
        return 0;
    }
    if ((fp->flags & FF_WRITE) == 0) {
        fp->err = 1;
        errno = EINVAL;
        return 0;
    }
    if (size != 0 && nmemb > ((size_t)-1) / size) {
        fp->err = 1;
        errno = EINVAL;
        return 0;
    }
    Bytes = size * nmemb;
    N = write(fp->fd, ptr, Bytes);
    if (N < 0) {
        fp->err = 1;
        return 0;
    }
    return (size_t)N / size;
}

int fseek(FILE *fp, long offset, int whence) {
    if (!fp) {
        errno = EINVAL;
        return -1;
    }
    if (lseek(fp->fd, (off_t)offset, whence) < 0) {
        fp->err = 1;
        return -1;
    }
    fp->eof = 0;
    return 0;
}

long ftell(FILE *fp) {
    off_t Pos;

    if (!fp) {
        errno = EINVAL;
        return -1;
    }
    Pos = lseek(fp->fd, 0, SEEK_CUR);
    if (Pos < 0) {
        fp->err = 1;
        return -1;
    }
    return (long)Pos;
}

int feof(FILE *fp) {
    return fp ? fp->eof : 0;
}

int ferror(FILE *fp) {
    return fp ? fp->err : 1;
}

int fileno(FILE *fp) {
    if (!fp) {
        errno = EINVAL;
        return -1;
    }
    return fp->fd;
}
