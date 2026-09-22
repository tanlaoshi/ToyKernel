/*
 * StoreInstall.c — 单包安装与登记
 * 拷贝切片：StoreInstallCopy.c。组合包：StoreCombo.c。
 */
#include "Store.h"
#include "StorePrivate.h"
#include "FileSystem.h"
#include "Fat.h"
#include "HalConsole.h"
#include "Hal.h"
#include "Db.h"

#define PUMP_IDLE    0
#define PUMP_COPY    1
#define PUMP_MARK    2

typedef struct {
    int Active;
    int Phase;
    int Kind;
    int Check;
    char Id[STORE_ID_MAX];
    char Type[12];
    char File[STORE_FILE_MAX];
    char Dst[96];
    char DepBuf[STORE_DEPENDS_MAX];
} STORE_PUMP_CTX;

static STORE_PUMP_CTX sPump;

static int StoreMarkInstalled(const char *Id, const char *Type, const char *File,
                              const char *Depends);

static int ResolveInstallSrc(const char *Id, const char *File, char *Out, int OutMax) {
    char Src[128];
    char Pkg[160];
    FAT_FILE_STAT St;

    JoinPath(Src, (int)sizeof(Src), STORE_CACHE_DIR, File);
    if (FileSystemFileStat(Src, &St) == FAT_OK && !(St.Attr & FAT_ATTR_DIR)) {
        CopyStr(Out, OutMax, Src);
        return FAT_OK;
    }
    JoinPath(Pkg, (int)sizeof(Pkg), "Assets/Store/packages", Id);
    JoinPath(Src, (int)sizeof(Src), Pkg, File);
    if (FileSystemFileStat(Src, &St) == FAT_OK && !(St.Attr & FAT_ATTR_DIR)) {
        CopyStr(Out, OutMax, Src);
        return FAT_OK;
    }
    if (FileSystemFileStat(File, &St) == FAT_OK && !(St.Attr & FAT_ATTR_DIR)) {
        CopyStr(Out, OutMax, File);
        return FAT_OK;
    }
    return FAT_ERR_NOENT;
}

void StoreInstallPumpAbort(void) {
    StoreInstallCopyAbort();
    sPump.Active = 0;
    sPump.Phase = PUMP_IDLE;
    sPump.Id[0] = 0;
}

int StoreInstallPumpBusy(void) {
    return (sPump.Active || StoreInstallCopyBusy()) ? 1 : 0;
}

void StoreInstallPumpProgress(UINTN *OutGot, UINTN *OutSize) {
    StoreInstallCopyProgress(OutGot, OutSize);
}

int StoreInstallPump(const char *Id) {
    STORE_ENTRY *Tab = gStoreTab;
    int Count = 0;
    int i;
    int Err;
    char Src[128];

    if (Id && Id[0]) {
        if (sPump.Active) {
            StoreInstallPumpAbort();
        }
        Err = StoreLoadCatalog(Tab, STORE_ENTRIES_MAX, &Count);
        if (Err < 0) {
            return Err;
        }
        for (i = 0; i < Count; i++) {
            if (!StrEq(Tab[i].Id, Id)) {
                continue;
            }
            sPump.Kind = EntryKind(Tab[i].Type);
            if (sPump.Kind < 0) {
                HalConsoleWriteSerial("store: bad type (app|font|asset|lib)\n");
                return FAT_ERR_INVAL;
            }
            if (!ArchOk(Tab[i].Arch)) {
                HalConsoleWriteSerial("store: arch mismatch\n");
                return FAT_ERR_INVAL;
            }
            CopyStr(sPump.Id, STORE_ID_MAX, Tab[i].Id);
            CopyStr(sPump.Type, (int)sizeof(sPump.Type), Tab[i].Type);
            CopyStr(sPump.File, STORE_FILE_MAX, Tab[i].File);
            CopyStr(sPump.DepBuf, (int)sizeof(sPump.DepBuf), Tab[i].Depends);
            if (LoadPkgDepends(Tab[i].Id, sPump.DepBuf, (int)sizeof(sPump.DepBuf))) {
                /* PKG 覆盖 */
            }
            NormalizeDepends(sPump.DepBuf);
            Err = CheckDependsInstalled(sPump.DepBuf);
            if (Err != FAT_OK) {
                return Err;
            }
            if (sPump.Kind == STORE_KIND_APP) {
                Err = EnsureAppsDir();
                if (Err != FAT_OK) {
                    return Err;
                }
                JoinPath(sPump.Dst, (int)sizeof(sPump.Dst), STORE_APPS_DIR, Tab[i].File);
                sPump.Check = STORE_CHECK_ELF;
            } else if (sPump.Kind == STORE_KIND_FONT) {
                Err = EnsureFontsDir();
                if (Err != FAT_OK) {
                    return Err;
                }
                JoinPath(sPump.Dst, (int)sizeof(sPump.Dst), STORE_FONTS_DIR, Tab[i].File);
                sPump.Check = STORE_CHECK_TOYF;
            } else {
                Err = EnsurePacksDir();
                if (Err != FAT_OK) {
                    return Err;
                }
                JoinPath(sPump.Dst, (int)sizeof(sPump.Dst), STORE_PACKS_DIR, Tab[i].File);
                sPump.Check = STORE_CHECK_NONE;
            }
            Err = ResolveInstallSrc(Tab[i].Id, Tab[i].File, Src, (int)sizeof(Src));
            if (Err != FAT_OK) {
                HalConsoleWriteSerial("store: install copy failed\n");
                return Err;
            }
            Err = StoreInstallCopyBegin(Src, sPump.Dst, sPump.Check);
            if (Err != FAT_OK) {
                HalConsoleWriteSerial("store: install copy failed\n");
                return Err;
            }
            sPump.Active = 1;
            sPump.Phase = PUMP_COPY;
            return StoreInstallPump(0);
        }
        return FAT_ERR_NOENT;
    }

    if (!sPump.Active) {
        return FAT_ERR_INVAL;
    }

    if (sPump.Phase == PUMP_COPY) {
        Err = StoreInstallCopyStep();
        if (Err == 1) {
            return 1;
        }
        if (Err != FAT_OK) {
            HalConsoleWriteSerial("store: install copy failed\n");
            StoreInstallPumpAbort();
            return Err;
        }
        sPump.Phase = PUMP_MARK;
        /* fall through same Step：登记很快 */
    }

    if (sPump.Kind == STORE_KIND_FONT) {
        gNeedFontReload = 1;
        if (gStoreComboDepth == 0) {
            StoreFlushFontReload();
        }
    }
    Err = StoreMarkInstalled(sPump.Id, sPump.Type, sPump.File, sPump.DepBuf);
    sPump.Active = 0;
    sPump.Phase = PUMP_IDLE;
    return Err == FAT_OK ? FAT_OK : Err;
}

int StoreInstall(const char *Id) {
    int Err;

    Err = StoreInstallPump(Id);
    while (Err == 1) {
        Err = StoreInstallPump(0);
    }
    return Err;
}

static int StoreMarkInstalled(const char *Id, const char *Type, const char *File,
                              const char *Depends) {
    char Key[DB_KEY_MAX];
    char DepKey[DB_KEY_MAX];
    char Val[DB_VAL_MAX];
    int i = 0;
    int j;

    if (!MakeDbKey(Key, (int)sizeof(Key), "si.", Id)) {
        return FAT_ERR_INVAL;
    }
    for (j = 0; Type && Type[j] && i < (int)sizeof(Val) - 1; j++) {
        Val[i++] = Type[j];
    }
    if (i < (int)sizeof(Val) - 1) {
        Val[i++] = '|';
    }
    for (j = 0; File && File[j] && i < (int)sizeof(Val) - 1; j++) {
        Val[i++] = File[j];
    }
    Val[i] = 0;
    if (DbSet(Key, Val) != DB_OK) {
        return FAT_ERR_IO;
    }
    if (MakeDbKey(DepKey, (int)sizeof(DepKey), "sd.", Id)) {
        if (Depends && Depends[0]) {
            (void)DbSet(DepKey, Depends);
        } else {
            (void)DbSet(DepKey, "-");
        }
    }
    return FAT_OK;
}

int StoreAdoptInstalled(const char *Id) {
    STORE_ENTRY *Tab = gStoreTab;
    int Count = 0;
    int i;
    int Err;
    char DepBuf[STORE_DEPENDS_MAX];

    if (StoreHasSi(Id)) {
        return FAT_OK;
    }
    Err = StoreLoadCatalog(Tab, STORE_ENTRIES_MAX, &Count);
    if (Err < 0) {
        return Err;
    }
    for (i = 0; i < Count; i++) {
        if (!StrEq(Tab[i].Id, Id)) {
            continue;
        }
        CopyStr(DepBuf, (int)sizeof(DepBuf), Tab[i].Depends);
        if (LoadPkgDepends(Tab[i].Id, DepBuf, (int)sizeof(DepBuf))) {
            /* PKG 覆盖 */
        }
        NormalizeDepends(DepBuf);
        return StoreMarkInstalled(Tab[i].Id, Tab[i].Type, Tab[i].File, DepBuf);
    }
    return FAT_ERR_NOENT;
}
