/*
 * StoreRemove.c — 卸装已装包
 * 核心：Store.c。查询仍在 Store.c。
 */
#include "Store.h"
#include "StorePrivate.h"
#include "FileSystem.h"
#include "Fat.h"
#include "HalConsole.h"
#include "Hal.h"
#include "Db.h"

/* 删托管载荷并刷盘；已 Resolve 存在时，勿把 NOENT 当成功。
 * 同名多目录项时循环摘除，直到 Dir 扫不到。 */
static int StoreUnlinkPayload(const char *Dir, const char *Want) {
    char Dst[96];
    char Prefixed[112];
    char Leaf[STORE_FILE_MAX];
    FAT_FILE_STAT St;
    int Err;
    int Round;

    if (!Dir || !Want || Want[0] == 0) {
        return FAT_ERR_INVAL;
    }

    for (Round = 0; Round < 8; Round++) {
        if (!DirResolveFileCI(Dir, Want, Leaf, (int)sizeof(Leaf))) {
            return FAT_OK;
        }
        JoinPath(Dst, (int)sizeof(Dst), Dir, Leaf);
        StoreIoBreath();
        Err = StoreDeleteManagedFile(Dst);
        (void)FileSystemFileSync("");
        if (Err == FAT_ERR_NOENT) {
            int n = 0;
            const char *Pre = "TOYOS:";
            while (Pre[n]) {
                Prefixed[n] = Pre[n];
                n++;
            }
            CopyStr(Prefixed + n, (int)sizeof(Prefixed) - n, Dst);
            Err = StoreDeleteManagedFile(Prefixed);
            (void)FileSystemFileSync("");
        }
        if (Err != FAT_OK && Err != FAT_ERR_NOENT) {
            return Err;
        }
        if (!DirHasFileCI(Dir, Want) && FileSystemFileStat(Dst, &St) != FAT_OK) {
            return FAT_OK;
        }
    }
    JoinPath(Dst, (int)sizeof(Dst), Dir, Want);
    if (DirHasFileCI(Dir, Want) || FileSystemFileStat(Dst, &St) == FAT_OK) {
        HalConsoleWriteSerial("store: remove failed (file remains) ");
        HalConsoleWriteSerial(Dst);
        HalConsoleWriteSerial("\n");
        return FAT_ERR_IO;
    }
    return FAT_OK;
}

int StoreRemove(const char *Id) {
    char Key[DB_KEY_MAX];
    char DepKey[DB_KEY_MAX];
    char Val[DB_VAL_MAX];
    char Type[12];
    char File[STORE_FILE_MAX];
    char Real[STORE_FILE_MAX];
    char Users[STORE_INSTALLED_MAX][STORE_ID_MAX];
    STORE_ENTRY *Tab;
    const char *Bar;
    const char *Dir;
    int i;
    int Kind;
    int Err;
    int UserN;
    int Count = 0;

    if (!Id || Id[0] == 0) {
        return FAT_ERR_INVAL;
    }
    if (!MakeDbKey(Key, (int)sizeof(Key), "si.", Id)) {
        return FAT_ERR_INVAL;
    }
    if (DbGet(Key, Val, sizeof(Val)) != DB_OK) {
        /*
         * 无 si.* 但仍标 installed（仅盘上有文件）：按 catalog 卸文件。
         * 未先 Install/Adopt 时直接 Remove 也能成。
         */
        Tab = gStoreTab;
        Err = StoreLoadCatalog(Tab, STORE_ENTRIES_MAX, &Count);
        if (Err < 0 || Count <= 0) {
            return FAT_ERR_NOENT;
        }
        for (i = 0; i < Count; i++) {
            if (!StrEq(Tab[i].Id, Id)) {
                continue;
            }
            Kind = EntryKind(Tab[i].Type);
            if (Kind == STORE_KIND_FONT) {
                Dir = STORE_FONTS_DIR;
            } else if (Kind == STORE_KIND_ASSET || Kind == STORE_KIND_LIB) {
                Dir = STORE_PACKS_DIR;
            } else if (Kind == STORE_KIND_APP) {
                Dir = STORE_APPS_DIR;
            } else {
                return FAT_ERR_INVAL;
            }
            if (!DirResolveFileCI(Dir, Tab[i].File, Real, (int)sizeof(Real))) {
                return FAT_ERR_NOENT;
            }
            UserN = CollectDependents(Id, Users, STORE_INSTALLED_MAX);
            if (UserN > 0) {
                HalConsoleWriteSerial("store: still required by dependents\n");
                return FAT_ERR_INVAL;
            }
            Err = StoreUnlinkPayload(Dir, Real);
            StoreIoBreath();
            if (Err != FAT_OK) {
                return Err;
            }
            if (Kind == STORE_KIND_FONT) {
                gNeedFontReload = 1;
                if (gStoreComboDepth == 0) {
                    StoreFlushFontReload();
                }
            }
            return FAT_OK;
        }
        return FAT_ERR_NOENT;
    }

    /* PR-M2：仍有已装包依赖本 Id → 拒绝（先卸上层） */
    UserN = CollectDependents(Id, Users, STORE_INSTALLED_MAX);
    if (UserN > 0) {
        HalConsoleWriteSerial("store: still required by:");
        for (i = 0; i < UserN; i++) {
            HalConsoleWriteSerial(" ");
            HalConsoleWriteSerial(Users[i]);
        }
        HalConsoleWriteSerial("\n");
        HalConsoleWriteSerial("hint: store remove <user> first, or store uncombo <leaf>\n");
        return FAT_ERR_INVAL;
    }

    Bar = Val;
    while (*Bar && *Bar != '|') {
        Bar++;
    }
    i = 0;
    while (Val + i < Bar && i < (int)sizeof(Type) - 1) {
        Type[i] = Val[i];
        i++;
    }
    Type[i] = 0;
    File[0] = 0;
    if (*Bar == '|') {
        Bar++;
        for (i = 0; Bar[i] && i < STORE_FILE_MAX - 1; i++) {
            File[i] = Bar[i];
        }
        File[i] = 0;
    }
    if (File[0] == 0) {
        (void)DbDelete(Key);
        return FAT_ERR_INVAL;
    }

    Kind = EntryKind(Type);
    if (Kind == STORE_KIND_FONT) {
        Dir = STORE_FONTS_DIR;
    } else if (Kind == STORE_KIND_ASSET || Kind == STORE_KIND_LIB) {
        Dir = STORE_PACKS_DIR;
    } else {
        Dir = STORE_APPS_DIR;
    }
    if (!DirResolveFileCI(Dir, File, Real, (int)sizeof(Real))) {
        /* si 指向已无文件：仍清 DB，算卸装成功 */
        (void)DbDelete(Key);
        if (MakeDbKey(DepKey, (int)sizeof(DepKey), "sd.", Id)) {
            (void)DbDelete(DepKey);
        }
        return FAT_OK;
    }

    Err = StoreUnlinkPayload(Dir, Real);
    StoreIoBreath();
    if (Err != FAT_OK) {
        return Err;
    }
    (void)DbDelete(Key);
    if (MakeDbKey(DepKey, (int)sizeof(DepKey), "sd.", Id)) {
        (void)DbDelete(DepKey);
    }
    if (Kind == STORE_KIND_FONT) {
        gNeedFontReload = 1;
        if (gStoreComboDepth == 0) {
            StoreFlushFontReload();
        }
    }
    return FAT_OK;
}


