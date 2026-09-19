/*
 * StoreDb.c — ToyDB 清单键与包类型查询
 * 核心：Store.c
 */
#include "Store.h"
#include "StorePriv.h"
#include "Db.h"

int MakeDbKey(char *Out, int Max, const char *Prefix, const char *Id) {
    int i = 0;
    int j;

    if (!Out || Max <= 0 || !Prefix || !Id || !Id[0]) {
        return 0;
    }
    for (j = 0; Prefix[j] && i < Max - 1; j++) {
        Out[i++] = Prefix[j];
    }
    for (j = 0; Id[j] && i < Max - 1; j++) {
        Out[i++] = Id[j];
    }
    Out[i] = 0;
    return Id[j] == 0;
}

/* si.* 或 catalog 的 type → STORE_KIND_*；未知 -1 */
int LookupPackageKind(const char *Id) {
    char Key[DB_KEY_MAX];
    char Val[DB_VAL_MAX];
    char Type[12];
    STORE_ENTRY *Tab;
    int Count = 0;
    int i;
    int Err;

    if (!Id || Id[0] == 0) {
        return -1;
    }
    if (MakeDbKey(Key, (int)sizeof(Key), "si.", Id) &&
        DbGet(Key, Val, sizeof(Val)) == DB_OK) {
        i = 0;
        while (Val[i] && Val[i] != '|' && i < (int)sizeof(Type) - 1) {
            Type[i] = Val[i];
            i++;
        }
        Type[i] = 0;
        return EntryKind(Type);
    }
    Tab = gStoreTab;
    Err = StoreLoadCatalog(Tab, STORE_ENTRIES_MAX, &Count);
    if (Err < 0 || Count <= 0) {
        return -1;
    }
    for (i = 0; i < Count; i++) {
        if (StrEq(Tab[i].Id, Id)) {
            return EntryKind(Tab[i].Type);
        }
    }
    return -1;
}

int StoreHasSi(const char *Id) {
    char Key[DB_KEY_MAX];
    char Val[DB_VAL_MAX];

    if (!Id || Id[0] == 0) {
        return 0;
    }
    if (!MakeDbKey(Key, (int)sizeof(Key), "si.", Id)) {
        return 0;
    }
    return DbGet(Key, Val, sizeof(Val)) == DB_OK ? 1 : 0;
}
