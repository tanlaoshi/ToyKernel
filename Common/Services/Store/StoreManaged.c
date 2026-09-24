/*
 * StoreManaged.c — 托管载荷判定与内部删除
 * 核心：Store.c
 */
#include "Store.h"
#include "StorePrivate.h"
#include "FileSystem.h"
#include "Fat.h"
#include "Db.h"

static int gStorePayloadBypass;

int StorePayloadBypassActive(void) {
    return gStorePayloadBypass > 0;
}

int StoreDeleteManagedFile(const char *Path) {
    int Err;

    gStorePayloadBypass++;
    Err = FileSystemDeleteFile(Path);
    if (gStorePayloadBypass > 0) {
        gStorePayloadBypass--;
    }
    return Err;
}

int StoreDeleteManagedTree(const char *Path) {
    int Err;

    gStorePayloadBypass++;
    Err = FileSystemRemoveTree(Path);
    if (gStorePayloadBypass > 0) {
        gStorePayloadBypass--;
    }
    return Err;
}

int StoreUnregister(const char *Id) {
    char Key[DB_KEY_MAX];
    char DepKey[DB_KEY_MAX];

    if (!Id || Id[0] == 0) {
        return FAT_ERR_INVAL;
    }
    if (!MakeDbKey(Key, (int)sizeof(Key), "si.", Id)) {
        return FAT_ERR_INVAL;
    }
    (void)DbDelete(Key);
    if (MakeDbKey(DepKey, (int)sizeof(DepKey), "sd.", Id)) {
        (void)DbDelete(DepKey);
    }
    return FAT_OK;
}

static int PathEndsWithElf(const char *Name) {
    int N = 0;

    if (!Name) {
        return 0;
    }
    while (Name[N]) {
        N++;
    }
    if (N < 4) {
        return 0;
    }
    return StrEqIgnoreCase(Name + N - 4, ".elf");
}

static int RelUnderDir(const char *Rel, const char *Dir, const char **OutLeaf) {
    int i = 0;

    if (!Rel || !Dir || !OutLeaf) {
        return 0;
    }
    while (Rel[0] == '/' || Rel[0] == '\\') {
        Rel++;
    }
    while (Dir[i]) {
        char Ca = Dir[i];
        char Cb = Rel[i];
        if (Ca >= 'a' && Ca <= 'z') {
            Ca = (char)(Ca - 'a' + 'A');
        }
        if (Cb >= 'a' && Cb <= 'z') {
            Cb = (char)(Cb - 'a' + 'A');
        }
        if (Ca != Cb) {
            return 0;
        }
        i++;
    }
    if (Rel[i] != '/' && Rel[i] != '\\') {
        return 0;
    }
    *OutLeaf = Rel + i + 1;
    return (*OutLeaf)[0] != 0;
}

static int SiListsFile(const char *File) {
    STORE_INSTALLED Inst[STORE_INSTALLED_MAX];
    int N = 0;
    int i;

    if (!File || !File[0]) {
        return 0;
    }
    if (StoreListInstalled(Inst, STORE_INSTALLED_MAX, &N) != FAT_OK) {
        return 0;
    }
    for (i = 0; i < N; i++) {
        if (StrEqIgnoreCase(StorePathBaseName(Inst[i].File), StorePathBaseName(File))) {
            return 1;
        }
    }
    return 0;
}

static int CatalogListsPayload(const char *File, int WantKind) {
    STORE_ENTRY *Tab = gStoreTab;
    int Count = 0;
    int i;

    if (!File || !File[0]) {
        return 0;
    }
    if (StoreLoadCatalog(Tab, STORE_ENTRIES_MAX, &Count) < 0 || Count <= 0) {
        return 0;
    }
    for (i = 0; i < Count; i++) {
        if (EntryKind(Tab[i].Type) == WantKind &&
            StrEqIgnoreCase(StorePathBaseName(Tab[i].File),
                            StorePathBaseName(File))) {
            return 1;
        }
    }
    return 0;
}

int StoreIsManagedPayload(const char *Path) {
    const char *Rel = Path;
    const char *Leaf = 0;
    int Vol;

    if (!Path || Path[0] == 0) {
        return 0;
    }
    if (FileSystemResolve(Path, &Vol, &Rel) != FAT_OK) {
        Rel = Path;
    }
    (void)Vol;
    if (RelUnderDir(Rel, STORE_APPS_DIR, &Leaf)) {
        const char *Base = StorePathBaseName(Leaf);

        if (!PathEndsWithElf(Base)) {
            return 0; /* Apps 下 README 等可删 */
        }
        return SiListsFile(Base) || CatalogListsPayload(Base, STORE_KIND_APP);
    }
    if (RelUnderDir(Rel, STORE_FONTS_DIR, &Leaf)) {
        return SiListsFile(Leaf) || CatalogListsPayload(Leaf, STORE_KIND_FONT);
    }
    if (RelUnderDir(Rel, STORE_PACKS_DIR, &Leaf)) {
        return SiListsFile(Leaf) || CatalogListsPayload(Leaf, STORE_KIND_ASSET);
    }
    return 0;
}
