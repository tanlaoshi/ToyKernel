/*
 * StoreQuery.c — 已装清单与依赖查询
 * 核心：Store.c
 */
#include "Store.h"
#include "StorePrivate.h"
#include "FileSystem.h"
#include "Fat.h"
#include "Db.h"

typedef struct {
    STORE_INSTALLED *Out;
    int Max;
    int Count;
} STORE_LIST_CTX;

static int ListInstalledCb(const char *Key, const char *Value, void *Ctx) {
    STORE_LIST_CTX *C = (STORE_LIST_CTX *)Ctx;
    STORE_INSTALLED *E;
    const char *Bar;
    int i;

    if (!Key || Key[0] != 's' || Key[1] != 'i' || Key[2] != '.') {
        return 0;
    }
    if (!C || !C->Out || C->Count >= C->Max) {
        return 1;
    }
    E = &C->Out[C->Count];
    for (i = 0; Key[3 + i] && i < STORE_ID_MAX - 1; i++) {
        E->Id[i] = Key[3 + i];
    }
    E->Id[i] = 0;
    E->Type[0] = 0;
    E->File[0] = 0;
    if (!Value) {
        C->Count++;
        return 0;
    }
    Bar = Value;
    while (*Bar && *Bar != '|') {
        Bar++;
    }
    {
        int n = 0;
        while (Value + n < Bar && n < (int)sizeof(E->Type) - 1) {
            E->Type[n] = Value[n];
            n++;
        }
        E->Type[n] = 0;
    }
    if (*Bar == '|') {
        Bar++;
        for (i = 0; Bar[i] && i < STORE_FILE_MAX - 1; i++) {
            E->File[i] = Bar[i];
        }
        E->File[i] = 0;
    }
    C->Count++;
    return 0;
}

int StoreListInstalled(STORE_INSTALLED *Out, int Max, int *OutCount) {
    STORE_LIST_CTX Ctx;

    if (!Out || Max <= 0 || !OutCount) {
        return FAT_ERR_INVAL;
    }
    Ctx.Out = Out;
    Ctx.Max = Max;
    Ctx.Count = 0;
    (void)DbForEach(ListInstalledCb, &Ctx);
    *OutCount = Ctx.Count;
    return FAT_OK;
}

int StoreIsInstalled(const char *Id) {
    char Key[DB_KEY_MAX];
    char DepKey[DB_KEY_MAX];
    char Val[DB_VAL_MAX];
    char Type[12];
    char File[STORE_FILE_MAX];
    const char *Bar;
    const char *Dir;
    STORE_ENTRY *Tab;
    int Count = 0;
    int i;
    int Kind;
    int Err;

    if (!Id || Id[0] == 0) {
        return 0;
    }
    if (!MakeDbKey(Key, (int)sizeof(Key), "si.", Id)) {
        return 0;
    }
    if (DbGet(Key, Val, sizeof(Val)) == DB_OK) {
        /* si.* 须对应盘上文件；否则清孤儿清单（Files 曾直删时） */
        Bar = Val;
        i = 0;
        while (Val[i] && Val[i] != '|' && i < (int)sizeof(Type) - 1) {
            Type[i] = Val[i];
            i++;
        }
        Type[i] = 0;
        File[0] = 0;
        while (*Bar && *Bar != '|') {
            Bar++;
        }
        if (*Bar == '|') {
            Bar++;
            for (i = 0; Bar[i] && i < STORE_FILE_MAX - 1; i++) {
                File[i] = Bar[i];
            }
            File[i] = 0;
        }
        Kind = EntryKind(Type);
        if (Kind == STORE_KIND_FONT) {
            Dir = STORE_FONTS_DIR;
        } else if (Kind == STORE_KIND_ASSET || Kind == STORE_KIND_LIB) {
            Dir = STORE_PACKS_DIR;
        } else if (Kind == STORE_KIND_APP) {
            if (File[0] && StoreAppElfExists(Id, File)) {
                return 1;
            }
            (void)DbDelete(Key);
            if (MakeDbKey(DepKey, (int)sizeof(DepKey), "sd.", Id)) {
                (void)DbDelete(DepKey);
            }
            return 0;
        } else {
            Dir = STORE_APPS_DIR;
        }
        if (File[0] && DirHasFileCI(Dir, File)) {
            return 1;
        }
        (void)DbDelete(Key);
        if (MakeDbKey(DepKey, (int)sizeof(DepKey), "sd.", Id)) {
            (void)DbDelete(DepKey);
        }
        return 0;
    }
    /*
     * 镜像预置（Apps/HELLO.ELF 等）无 si.* 时：按 catalog 落盘路径探测。
     * 开始菜单扫目录能看到，商店也应标 installed。
     */
    Tab = gStoreTab;
    Err = StoreLoadCatalog(Tab, STORE_ENTRIES_MAX, &Count);
    if (Err < 0 || Count <= 0) {
        return 0;
    }
    for (i = 0; i < Count; i++) {
        if (!StrEq(Tab[i].Id, Id)) {
            continue;
        }
        Kind = EntryKind(Tab[i].Type);
        if (Kind == STORE_KIND_APP) {
            return StoreAppElfExists(Tab[i].Id, Tab[i].File);
        }
        if (Kind == STORE_KIND_FONT) {
            return DirHasFileCI(STORE_FONTS_DIR, Tab[i].File);
        }
        if (Kind == STORE_KIND_ASSET || Kind == STORE_KIND_LIB) {
            return DirHasFileCI(STORE_PACKS_DIR, Tab[i].File);
        }
        return 0;
    }
    return 0;
}

void StoreFillInstalledFlags(const STORE_ENTRY *Tab, int Count, int *OutFlags) {
    static FAT_DIRECTORY_ENTRY Fonts[FAT_LIST_MAX];
    static FAT_DIRECTORY_ENTRY Packs[FAT_LIST_MAX];
    char Key[DB_KEY_MAX];
    char Val[DB_VAL_MAX];
    int Nf = -1;
    int Np = -1;
    int i;
    int j;
    int Kind;

    if (!OutFlags) {
        return;
    }
    if (!Tab || Count <= 0) {
        return;
    }
    if (Count > STORE_ENTRIES_MAX) {
        Count = STORE_ENTRIES_MAX;
    }

    for (i = 0; i < Count; i++) {
        OutFlags[i] = 0;
        if (!Tab[i].Id[0]) {
            continue;
        }
        if (MakeDbKey(Key, (int)sizeof(Key), "si.", Tab[i].Id) &&
            DbGet(Key, Val, sizeof(Val)) == DB_OK) {
            /* 与 StoreIsInstalled 一致：有 si 无文件 → 不算已装（清孤儿） */
            OutFlags[i] = StoreIsInstalled(Tab[i].Id) ? 1 : 0;
            continue;
        }
        Kind = EntryKind(Tab[i].Type);
        if (Kind == STORE_KIND_APP) {
            OutFlags[i] = StoreAppElfExists(Tab[i].Id, Tab[i].File) ? 1 : 0;
        } else if (Kind == STORE_KIND_FONT) {
            if (Nf < 0) {
                Nf = 0;
                if (FileSystemListEntries(STORE_FONTS_DIR, Fonts, FAT_LIST_MAX, &Nf) !=
                    FAT_OK) {
                    Nf = 0;
                }
                StoreIoBreath();
            }
            for (j = 0; j < Nf; j++) {
                if (!(Fonts[j].Attr & FAT_ATTR_DIR) &&
                    StrEqIgnoreCase(Fonts[j].Name, Tab[i].File)) {
                    OutFlags[i] = 1;
                    break;
                }
            }
        } else if (Kind == STORE_KIND_ASSET) {
            if (Np < 0) {
                Np = 0;
                if (FileSystemListEntries(STORE_PACKS_DIR, Packs, FAT_LIST_MAX, &Np) !=
                    FAT_OK) {
                    Np = 0;
                }
                StoreIoBreath();
            }
            for (j = 0; j < Np; j++) {
                if (!(Packs[j].Attr & FAT_ATTR_DIR) &&
                    StrEqIgnoreCase(Packs[j].Name, Tab[i].File)) {
                    OutFlags[i] = 1;
                    break;
                }
            }
        }
    }
}

int StoreGetDepends(const char *Id, char *Out, int OutMax) {
    char Key[DB_KEY_MAX];
    char Val[DB_VAL_MAX];
    int i;

    if (!Out || OutMax <= 0) {
        return FAT_ERR_INVAL;
    }
    Out[0] = '-';
    if (OutMax > 1) {
        Out[1] = 0;
    } else {
        Out[0] = 0;
        return FAT_ERR_INVAL;
    }
    if (!Id || Id[0] == 0) {
        return FAT_OK;
    }
    if (!MakeDbKey(Key, (int)sizeof(Key), "sd.", Id)) {
        return FAT_OK;
    }
    if (DbGet(Key, Val, sizeof(Val)) == DB_OK && Val[0] != 0 &&
        !(Val[0] == '-' && Val[1] == 0)) {
        for (i = 0; Val[i] && i < OutMax - 1; i++) {
            Out[i] = Val[i];
        }
        Out[i] = 0;
        return FAT_OK;
    }
    /* sd 缺失/"-"：回退 catalog+PKG（list 与 CollectDependents 一致） */
    if (ResolveEntryDepends(Id, Out, OutMax) == FAT_OK && Out[0] != 0 &&
        !(Out[0] == '-' && Out[1] == 0)) {
        return FAT_OK;
    }
    Out[0] = '-';
    if (OutMax > 1) {
        Out[1] = 0;
    }
    return FAT_OK;
}
