/*
 * StoreInstallBundle.c — PR-S-bundle-install：app → Apps/<id>/
 *
 * si.file 仍为 ELF 基名（方案 A）；落点 Apps/<id>/<file>。
 * 额外拷 PKG.TXT 与 packages/<id>/Assets/（若有）。
 */
#include "Store.h"
#include "StorePrivate.h"
#include "FileSystem.h"
#include "Fat.h"

#define BUNDLE_PATH_MAX 160

static int PathHasSlash(const char *S) {
    if (!S) {
        return 0;
    }
    while (*S) {
        if (*S == '/' || *S == '\\') {
            return 1;
        }
        S++;
    }
    return 0;
}

const char *StorePathBaseName(const char *Path) {
    const char *Base = Path;

    if (!Path) {
        return "";
    }
    while (*Path) {
        if (*Path == '/' || *Path == '\\') {
            Base = Path + 1;
        }
        Path++;
    }
    return Base ? Base : "";
}

void StoreAppBundleDir(char *Out, int Max, const char *Id) {
    JoinPath(Out, Max, STORE_APPS_DIR, Id ? Id : "");
}

void StoreAppElfPath(char *Out, int Max, const char *Id, const char *File) {
    char Dir[BUNDLE_PATH_MAX];

    StoreAppBundleDir(Dir, (int)sizeof(Dir), Id);
    JoinPath(Out, Max, Dir, File ? File : "");
}

int StoreAppElfExists(const char *Id, const char *File) {
    char Path[BUNDLE_PATH_MAX];
    FAT_FILE_STAT St;

    if (!File || !File[0]) {
        return 0;
    }
    if (Id && Id[0]) {
        StoreAppElfPath(Path, (int)sizeof(Path), Id, File);
        if (FileSystemFileStat(Path, &St) == FAT_OK && !(St.Attr & FAT_ATTR_DIR)) {
            return 1;
        }
    }
    return DirHasFileCI(STORE_APPS_DIR, File) ? 1 : 0;
}

int StoreAppBundleReady(const char *Id) {
    STORE_ENTRY *Tab = gStoreTab;
    char Path[BUNDLE_PATH_MAX];
    FAT_FILE_STAT St;
    int Count = 0;
    int i;
    int Err;

    if (!Id || !Id[0]) {
        return 0;
    }
    Err = StoreLoadCatalog(Tab, STORE_ENTRIES_MAX, &Count);
    if (Err < 0 || Count <= 0) {
        return 0;
    }
    for (i = 0; i < Count; i++) {
        if (!StrEq(Tab[i].Id, Id)) {
            continue;
        }
        if (EntryKind(Tab[i].Type) != STORE_KIND_APP) {
            return 0;
        }
        StoreAppElfPath(Path, (int)sizeof(Path), Id, Tab[i].File);
        return (FileSystemFileStat(Path, &St) == FAT_OK &&
                !(St.Attr & FAT_ATTR_DIR))
                   ? 1
                   : 0;
    }
    return 0;
}

int StoreResolveAppPath(const char *Id, const char *File, char *Out, int OutMax) {
    char Path[BUNDLE_PATH_MAX];
    FAT_FILE_STAT St;
    char Leaf[STORE_FILE_MAX];

    if (!Out || OutMax <= 0 || !File || !File[0]) {
        return FAT_ERR_INVAL;
    }
    if (Id && Id[0]) {
        StoreAppElfPath(Path, (int)sizeof(Path), Id, File);
        if (FileSystemFileStat(Path, &St) == FAT_OK && !(St.Attr & FAT_ATTR_DIR)) {
            CopyStr(Out, OutMax, Path);
            return FAT_OK;
        }
    }
    if (DirResolveFileCI(STORE_APPS_DIR, File, Leaf, (int)sizeof(Leaf))) {
        JoinPath(Out, OutMax, STORE_APPS_DIR, Leaf);
        return FAT_OK;
    }
    /* 偏好新布局路径（灰显 / 提示用） */
    if (Id && Id[0]) {
        StoreAppElfPath(Out, OutMax, Id, File);
    } else {
        JoinPath(Out, OutMax, STORE_APPS_DIR, File);
    }
    return FAT_ERR_NOENT;
}

static int CopyOneFile(const char *Src, const char *Dst) {
    return StoreInstallCopySync(Src, Dst, STORE_CHECK_NONE);
}

static int CopyDirTree(const char *SrcDir, const char *DstDir, int Depth) {
    static FAT_DIRECTORY_ENTRY Ents[FAT_LIST_MAX];
    char SrcChild[BUNDLE_PATH_MAX];
    char DstChild[BUNDLE_PATH_MAX];
    FAT_FILE_STAT St;
    int N;
    int i;
    int Err;
    int Pass;
    int Progress;

    if (Depth > 8) {
        return FAT_ERR_INVAL;
    }
    Err = FileSystemMakePath(DstDir);
    if (Err != FAT_OK) {
        return Err;
    }
    for (Pass = 0; Pass < FAT_LIST_MAX + 2; Pass++) {
        N = 0;
        Err = FileSystemListEntries(SrcDir, Ents, FAT_LIST_MAX, &N);
        if (Err != FAT_OK) {
            return Err;
        }
        Progress = 0;
        for (i = 0; i < N; i++) {
            const char *Name = Ents[i].Name;

            if (!Name[0] || (Name[0] == '.' && Name[1] == 0) ||
                (Name[0] == '.' && Name[1] == '.' && Name[2] == 0)) {
                continue;
            }
            JoinPath(SrcChild, (int)sizeof(SrcChild), SrcDir, Name);
            JoinPath(DstChild, (int)sizeof(DstChild), DstDir, Name);
            if (Ents[i].Attr & FAT_ATTR_DIR) {
                Err = CopyDirTree(SrcChild, DstChild, Depth + 1);
            } else {
                Err = CopyOneFile(SrcChild, DstChild);
            }
            if (Err != FAT_OK) {
                return Err;
            }
            Progress = 1;
            (void)St;
        }
        /* 一趟列全 FAT_LIST_MAX 以内即完；超限多趟意义有限，停 */
        if (N < FAT_LIST_MAX || !Progress) {
            break;
        }
    }
    return FAT_OK;
}

int StoreInstallBundlePrepare(const char *Id, const char *File, char *DstElf,
                              int DstMax) {
    char Dir[BUNDLE_PATH_MAX];
    int Err;

    if (!Id || !Id[0] || !File || !File[0] || !DstElf || DstMax <= 0) {
        return FAT_ERR_INVAL;
    }
    if (PathHasSlash(Id) || PathHasSlash(File)) {
        return FAT_ERR_INVAL;
    }
    Err = EnsureAppsDir();
    if (Err != FAT_OK) {
        return Err;
    }
    StoreAppBundleDir(Dir, (int)sizeof(Dir), Id);
    Err = FileSystemMakePath(Dir);
    if (Err != FAT_OK) {
        return Err;
    }
    StoreAppElfPath(DstElf, DstMax, Id, File);
    return FAT_OK;
}

int StoreInstallBundleExtras(const char *Id) {
    char PkgDir[BUNDLE_PATH_MAX];
    char Src[BUNDLE_PATH_MAX];
    char Dst[BUNDLE_PATH_MAX];
    char Bundle[BUNDLE_PATH_MAX];
    FAT_FILE_STAT St;
    int Err;

    if (!Id || !Id[0]) {
        return FAT_ERR_INVAL;
    }
    StoreAppBundleDir(Bundle, (int)sizeof(Bundle), Id);
    JoinPath(PkgDir, (int)sizeof(PkgDir), "Assets/Store/packages", Id);

    JoinPath(Src, (int)sizeof(Src), PkgDir, "PKG.TXT");
    if (FileSystemFileStat(Src, &St) == FAT_OK && !(St.Attr & FAT_ATTR_DIR)) {
        JoinPath(Dst, (int)sizeof(Dst), Bundle, "PKG.TXT");
        Err = CopyOneFile(Src, Dst);
        if (Err != FAT_OK) {
            return Err;
        }
    }

    JoinPath(Src, (int)sizeof(Src), PkgDir, "Assets");
    if (FileSystemFileStat(Src, &St) == FAT_OK && (St.Attr & FAT_ATTR_DIR)) {
        JoinPath(Dst, (int)sizeof(Dst), Bundle, "Assets");
        Err = CopyDirTree(Src, Dst, 0);
        if (Err != FAT_OK) {
            return Err;
        }
    }
    return FAT_OK;
}
