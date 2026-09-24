/*
 * FileSystemTree.c — PR-S-bundle-fs：递归建路径 / 删树
 *
 * MakePath：逐级 mkdir（已存在目录视为成功）。
 * RemoveTree：先清子项再 rmdir；文件路径则 DeleteFile。
 */
#include "FileSystemPrivate.h"
#include "Store.h"
#include "Fat.h"

#define FS_TREE_PATH_MAX 192
#define FS_TREE_DEPTH_MAX 16

static int PathLen(const char *S) {
    int N = 0;
    if (!S) {
        return 0;
    }
    while (S[N]) {
        N++;
    }
    return N;
}

static void PathCopy(char *Dst, int Max, const char *Src, int N) {
    int i;
    if (Max <= 0) {
        return;
    }
    if (N >= Max) {
        N = Max - 1;
    }
    for (i = 0; i < N; i++) {
        Dst[i] = Src[i];
    }
    Dst[i] = 0;
}

/* 返回前缀长度（含冒号）；无前缀 → 0 */
static int VolPrefixLen(const char *Path) {
    int i;
    if (!Path) {
        return 0;
    }
    for (i = 0; Path[i]; i++) {
        if (Path[i] == ':') {
            return i + 1;
        }
    }
    return 0;
}

static int IsDotOrDotDot(const char *Name) {
    if (!Name || !Name[0]) {
        return 1;
    }
    if (Name[0] == '.' && Name[1] == 0) {
        return 1;
    }
    if (Name[0] == '.' && Name[1] == '.' && Name[2] == 0) {
        return 1;
    }
    return 0;
}

static int JoinUnder(char *Out, int OutMax, const char *Dir, const char *Name) {
    int Dl;
    int Nl;
    int i;
    int p;

    if (!Out || OutMax <= 0 || !Name || !Name[0]) {
        return FAT_ERR_INVAL;
    }
    Dl = Dir ? PathLen(Dir) : 0;
    Nl = PathLen(Name);
    if (Dl + 1 + Nl + 1 > OutMax) {
        return FAT_ERR_NAMETOOLONG;
    }
    p = 0;
    for (i = 0; i < Dl; i++) {
        Out[p++] = Dir[i];
    }
    if (Dl > 0 && Dir[Dl - 1] != '/' && Dir[Dl - 1] != ':') {
        Out[p++] = '/';
    }
    for (i = 0; i < Nl; i++) {
        Out[p++] = Name[i];
    }
    Out[p] = 0;
    return FAT_OK;
}

int FileSystemMakePath(const char *Path) {
    char Cum[FS_TREE_PATH_MAX];
    FAT_FILE_STAT St;
    int Pref;
    const char *Rel;
    int RelLen;
    int i;
    int Start;
    int Err;

    if (!Path || !Path[0]) {
        return FAT_ERR_INVAL;
    }
    Pref = VolPrefixLen(Path);
    Rel = Path + Pref;
    while (*Rel == '/' || *Rel == '\\') {
        Rel++;
    }
    RelLen = PathLen(Rel);
    if (RelLen <= 0) {
        return FAT_OK;
    }
    if (Pref + RelLen >= FS_TREE_PATH_MAX) {
        return FAT_ERR_NAMETOOLONG;
    }

    Start = 0;
    for (i = 0; i <= RelLen; i++) {
        if (i < RelLen && Rel[i] != '/' && Rel[i] != '\\') {
            continue;
        }
        if (i == Start) {
            Start = i + 1;
            continue;
        }
        PathCopy(Cum, sizeof(Cum), Path, Pref);
        PathCopy(Cum + Pref, (int)sizeof(Cum) - Pref, Rel, i);
        Err = FileSystemFileStat(Cum, &St);
        if (Err == FAT_OK) {
            if (!(St.Attr & FAT_ATTR_DIR)) {
                return FAT_ERR_NOTDIR;
            }
        } else if (Err == FAT_ERR_NOENT) {
            Err = FileSystemMakeDirectory(Cum);
            if (Err == FAT_ERR_EXIST) {
                Err = FileSystemFileStat(Cum, &St);
                if (Err != FAT_OK) {
                    return Err;
                }
                if (!(St.Attr & FAT_ATTR_DIR)) {
                    return FAT_ERR_NOTDIR;
                }
            } else if (Err != FAT_OK) {
                return Err;
            }
        } else {
            return Err;
        }
        Start = i + 1;
    }
    return FAT_OK;
}

static int RemoveTreeRec(const char *Path, int Depth) {
    static FAT_DIRECTORY_ENTRY Ents[FAT_LIST_MAX];
    char Child[FS_TREE_PATH_MAX];
    FAT_FILE_STAT St;
    int N;
    int i;
    int Err;
    int Pass;
    int Progress;

    if (Depth > FS_TREE_DEPTH_MAX) {
        return FAT_ERR_INVAL;
    }
    Err = FileSystemFileStat(Path, &St);
    if (Err == FAT_ERR_NOENT) {
        return FAT_OK;
    }
    if (Err != FAT_OK) {
        return Err;
    }
    if (!(St.Attr & FAT_ATTR_DIR)) {
        return FileSystemDeleteFile(Path);
    }

    /* 多趟：目录项可能 > FAT_LIST_MAX */
    for (Pass = 0; Pass < FAT_LIST_MAX + 4; Pass++) {
        N = 0;
        Err = FileSystemListEntries(Path, Ents, FAT_LIST_MAX, &N);
        if (Err != FAT_OK) {
            return Err;
        }
        Progress = 0;
        for (i = 0; i < N; i++) {
            if (IsDotOrDotDot(Ents[i].Name)) {
                continue;
            }
            Err = JoinUnder(Child, (int)sizeof(Child), Path, Ents[i].Name);
            if (Err != FAT_OK) {
                return Err;
            }
            if (Ents[i].Attr & FAT_ATTR_DIR) {
                Err = RemoveTreeRec(Child, Depth + 1);
            } else {
                Err = FileSystemDeleteFile(Child);
            }
            if (Err != FAT_OK) {
                return Err;
            }
            Progress = 1;
        }
        if (!Progress) {
            break;
        }
    }

    return FileSystemRemoveDirectory(Path);
}

int FileSystemRemoveTree(const char *Path) {
    if (!Path || !Path[0]) {
        return FAT_ERR_INVAL;
    }
    if (!StorePayloadBypassActive() && StoreIsManagedPayload(Path)) {
        return FAT_ERR_STORE;
    }
    return RemoveTreeRec(Path, 0);
}
