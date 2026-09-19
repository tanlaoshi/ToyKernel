/*
 * StoreCombo.c — 按依赖顺序装卸多包
 * 核心：Store.c。单包安装在 StoreInstall.c。
 */
#include "Store.h"
#include "StorePriv.h"
#include "Fat.h"
#include "HalConsole.h"
#include "Hal.h"
#include "Db.h"

/* 字体/资源包/库：可被多 app 引用，uncombo 只卸叶子 app，不级联卸这些 */
static int PackageIsSharedDep(int Kind) {
    return Kind == STORE_KIND_FONT ||
           Kind == STORE_KIND_ASSET ||
           Kind == STORE_KIND_LIB ||
           Kind < 0; /* 未知类型宁可保留 */
}

static int ComboInstallRec(const char *Id, int Depth) {
    char DepBuf[STORE_DEPENDS_MAX];
    char Tok[STORE_ID_MAX];
    const char *P;
    int n;
    int Err;

    if (!Id || Id[0] == 0) {
        return FAT_ERR_INVAL;
    }
    if (Depth > STORE_ENTRIES_MAX) {
        HalConsoleWriteSerial("store combo: depends cycle or too deep\n");
        return FAT_ERR_INVAL;
    }
    if (StoreIsInstalled(Id)) {
        /* 盘上已有但无 si.*（镜像预置）：补登记，便于随后 Remove */
        if (!StoreHasSi(Id)) {
            Err = StoreAdoptInstalled(Id);
            if (Err != FAT_OK) {
                return Err;
            }
        }
        return FAT_OK;
    }

    Err = ResolveEntryDepends(Id, DepBuf, (int)sizeof(DepBuf));
    if (Err != FAT_OK) {
        return Err;
    }

    P = DepBuf;
    while (*P) {
        while (*P == ',' || *P == ' ' || *P == '\t') {
            P++;
        }
        if (*P == 0) {
            break;
        }
        n = 0;
        while (*P && *P != ',' && n + 1 < STORE_ID_MAX) {
            if (*P != ' ' && *P != '\t') {
                Tok[n++] = *P;
            }
            P++;
        }
        Tok[n] = 0;
        if (Tok[0] == 0) {
            continue;
        }
        StoreIoBreath();
        Err = ComboInstallRec(Tok, Depth + 1);
        if (Err != FAT_OK) {
            return Err;
        }
    }

    HalConsoleWriteSerial("store combo: +");
    HalConsoleWriteSerial(Id);
    HalConsoleWriteSerial("\n");
    StoreIoBreath();
    return StoreInstall(Id);
}

int StoreComboInstall(const char *Id) {
    int Err;

    if (!Id || Id[0] == 0) {
        return FAT_ERR_INVAL;
    }
    gStoreComboDepth++;
    Err = ComboInstallRec(Id, 0);
    gStoreComboDepth--;
    if (gStoreComboDepth == 0) {
        StoreFlushFontReload();
    }
    return Err;
}

int StoreComboRemove(const char *Id) {
    char DepBuf[STORE_DEPENDS_MAX];
    char Tok[STORE_ID_MAX];
    char Deps[STORE_ENTRIES_MAX][STORE_ID_MAX];
    const char *P;
    int DepN = 0;
    int n;
    int i;
    int Err;
    int Users;

    if (!Id || Id[0] == 0) {
        return FAT_ERR_INVAL;
    }
    if (!StoreIsInstalled(Id)) {
        return FAT_ERR_NOENT;
    }

    DepBuf[0] = 0;
    (void)StoreGetDepends(Id, DepBuf, (int)sizeof(DepBuf));
    NormalizeDepends(DepBuf);
    if (DepBuf[0] == 0) {
        /* 无 sd.*（仅盘上 / 刚 Adopt 失败）：用 catalog 依赖做 uncombo */
        (void)ResolveEntryDepends(Id, DepBuf, (int)sizeof(DepBuf));
        NormalizeDepends(DepBuf);
    }

    P = DepBuf;
    while (*P && DepN < STORE_ENTRIES_MAX) {
        while (*P == ',' || *P == ' ' || *P == '\t') {
            P++;
        }
        if (*P == 0) {
            break;
        }
        n = 0;
        while (*P && *P != ',' && n + 1 < STORE_ID_MAX) {
            if (*P != ' ' && *P != '\t') {
                Tok[n++] = *P;
            }
            P++;
        }
        Tok[n] = 0;
        if (Tok[0]) {
            CopyStr(Deps[DepN], STORE_ID_MAX, Tok);
            DepN++;
        }
    }

    HalConsoleWriteSerial("store uncombo: -");
    HalConsoleWriteSerial(Id);
    HalConsoleWriteSerial("\n");
    gStoreComboDepth++;
    Err = StoreRemove(Id);
    if (Err != FAT_OK) {
        gStoreComboDepth--;
        if (gStoreComboDepth == 0) {
            StoreFlushFontReload();
        }
        return Err;
    }

    /*
     * 逆序卸依赖：
     * - font / asset / lib（公共依赖）一律保留，哪怕暂时无人引用
     * - 仅级联卸 type=app 且已无其它包引用的依赖
     */
    for (i = DepN - 1; i >= 0; i--) {
        char UsersArr[STORE_INSTALLED_MAX][STORE_ID_MAX];
        int Kind;

        StoreIoBreath();
        if (!StoreIsInstalled(Deps[i])) {
            continue;
        }
        Kind = LookupPackageKind(Deps[i]);
        if (PackageIsSharedDep(Kind)) {
            HalConsoleWriteSerial("store uncombo: keep shared ");
            HalConsoleWriteSerial(Deps[i]);
            HalConsoleWriteSerial("\n");
            continue;
        }
        Users = CollectDependents(Deps[i], UsersArr, STORE_INSTALLED_MAX);
        if (Users > 0) {
            continue;
        }
        HalConsoleWriteSerial("store uncombo: -");
        HalConsoleWriteSerial(Deps[i]);
        HalConsoleWriteSerial("\n");
        Err = StoreRemove(Deps[i]);
        if (Err != FAT_OK && Err != FAT_ERR_NOENT) {
            gStoreComboDepth--;
            if (gStoreComboDepth == 0) {
                StoreFlushFontReload();
            }
            return Err;
        }
    }
    gStoreComboDepth--;
    if (gStoreComboDepth == 0) {
        StoreFlushFontReload();
    }
    return FAT_OK;
}
