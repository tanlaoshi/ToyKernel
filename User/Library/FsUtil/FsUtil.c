/*
 * FsUtil.c — libFsUtil（PR-A-fsutil）
 */
#include <errno.h>
#include <FsUtil.h>
#include <dirent.h>

static unsigned CopyCap(char *Dst, unsigned Cap, const char *Src) {
    unsigned I;

    if (!Dst || Cap == 0) {
        return 0;
    }
    if (!Src) {
        Dst[0] = 0;
        return 0;
    }
    I = 0;
    while (Src[I] != 0 && I + 1u < Cap) {
        Dst[I] = Src[I];
        I++;
    }
    Dst[I] = 0;
    return I;
}

int FsUtilHasVolume(const char *Path) {
    unsigned I;

    if (!Path) {
        return 0;
    }
    for (I = 0; Path[I] != 0; I++) {
        if (Path[I] == ':') {
            return I > 0;
        }
        if (Path[I] == '/') {
            return 0;
        }
    }
    return 0;
}

int FsUtilJoin(char *Out, unsigned Cap, const char *Dir, const char *Name) {
    unsigned N;
    unsigned I;
    int NeedSep;

    if (!Out || Cap == 0) {
        errno = EINVAL;
        return -1;
    }
    if (!Dir) {
        Dir = "";
    }
    if (!Name) {
        Name = "";
    }
    if (FsUtilHasVolume(Name) || Dir[0] == 0) {
        N = CopyCap(Out, Cap, Name);
        if (Name[N] != 0) {
            errno = EINVAL;
            return -1;
        }
        return 0;
    }
    if (Name[0] == 0) {
        N = CopyCap(Out, Cap, Dir);
        if (Dir[N] != 0) {
            errno = EINVAL;
            return -1;
        }
        return 0;
    }

    N = CopyCap(Out, Cap, Dir);
    if (Dir[N] != 0) {
        errno = EINVAL;
        return -1;
    }
    NeedSep = 1;
    if (N > 0 && (Out[N - 1] == ':' || Out[N - 1] == '/')) {
        NeedSep = 0;
    }
    if (Name[0] == '/') {
        Name++;
        NeedSep = (N > 0 && Out[N - 1] != ':' && Out[N - 1] != '/');
    }
    if (NeedSep) {
        if (N + 1u >= Cap) {
            errno = EINVAL;
            return -1;
        }
        Out[N] = '/';
        N++;
        Out[N] = 0;
    }
    I = 0;
    while (Name[I] != 0) {
        if (N + 1u >= Cap) {
            errno = EINVAL;
            return -1;
        }
        Out[N] = Name[I];
        N++;
        I++;
    }
    Out[N] = 0;
    return 0;
}

int FsUtilVolumePath(char *Out, unsigned Cap, const char *Vol, const char *Rel) {
    unsigned N;
    unsigned I;
    char VolBuf[16];

    if (!Out || Cap == 0 || !Vol || Vol[0] == 0) {
        errno = EINVAL;
        return -1;
    }
    if (!Rel) {
        Rel = "";
    }
    if (FsUtilHasVolume(Rel)) {
        return FsUtilJoin(Out, Cap, "", Rel);
    }
    I = 0;
    while (Vol[I] != 0 && Vol[I] != ':' && I + 1u < sizeof(VolBuf)) {
        VolBuf[I] = Vol[I];
        I++;
    }
    VolBuf[I] = 0;
    if (I == 0) {
        errno = EINVAL;
        return -1;
    }
    N = CopyCap(Out, Cap, VolBuf);
    if (N + 1u >= Cap) {
        errno = EINVAL;
        return -1;
    }
    Out[N] = ':';
    N++;
    Out[N] = 0;
    if (Rel[0] == '/') {
        Rel++;
    }
    I = 0;
    while (Rel[I] != 0) {
        if (N + 1u >= Cap) {
            errno = EINVAL;
            return -1;
        }
        Out[N] = Rel[I];
        N++;
        I++;
    }
    Out[N] = 0;
    return 0;
}

int FsUtilToyosPath(char *Out, unsigned Cap, const char *Rel) {
    return FsUtilVolumePath(Out, Cap, TOY_FS_VOL_TOYOS, Rel);
}

int FsUtilListDir(const char *Path, TOY_DIR_ENT *Out, int Max) {
    TOY_DIR *Dir;
    int Count;
    int Rc;

    if (!Out || Max <= 0) {
        errno = EINVAL;
        return -1;
    }
    Dir = OpenDirectory(Path ? Path : "");
    if (!Dir) {
        return -1;
    }
    Count = 0;
    while (Count < Max) {
        Rc = ReadDirectory(Dir, &Out[Count]);
        if (Rc < 0) {
            CloseDirectory(Dir);
            return -1;
        }
        if (Rc == 0) {
            break;
        }
        Count++;
    }
    CloseDirectory(Dir);
    return Count;
}

int FsUtilStat(const char *Path, TOY_FILE_STAT *Out) {
    return FileStat(Path ? Path : "", Out);
}

int FsUtilIsDir(const char *Path) {
    TOY_FILE_STAT St;

    if (FsUtilStat(Path, &St) != 0) {
        return -1;
    }
    return (St.Attr & TOY_ATTR_DIR) ? 1 : 0;
}
