/*
 * Db.c — TOYOS.DB 文本 KV（PR-DB1）
 * 读盘 / 写盘：DbFile.c
 */
#include "DbPrivate.h"
#include "Console.h"
#include "Debug.h"

DB_REC gRecs[DB_MAX_RECORDS];
int gReady;
int gDbDirty;
int gBatch;

int FindSlot(const char *Key) {
    int i;
    for (i = 0; i < DB_MAX_RECORDS; i++) {
        if (gRecs[i].Used && StrEq(gRecs[i].Key, Key)) {
            return i;
        }
    }
    return -1;
}

int AllocSlot(void) {
    int i;
    for (i = 0; i < DB_MAX_RECORDS; i++) {
        if (!gRecs[i].Used) {
            return i;
        }
    }
    return -1;
}

void ClearAll(void) {
    int i;
    for (i = 0; i < DB_MAX_RECORDS; i++) {
        gRecs[i].Used = 0;
        gRecs[i].Key[0] = 0;
        gRecs[i].Val[0] = 0;
    }
    gDbDirty = 0;
}

int DbGet(const char *Key, char *Out, UINTN OutMax) {
    int Slot;
    if (!gReady || !KeyOk(Key) || !Out || OutMax == 0) {
        return DB_INVAL;
    }
    Slot = FindSlot(Key);
    if (Slot < 0) {
        return DB_NOENT;
    }
    CopyStr(Out, (int)OutMax, gRecs[Slot].Val);
    return DB_OK;
}

int DbSet(const char *Key, const char *Value) {
    int Slot;
    if (!gReady || !KeyOk(Key) || !Value) {
        return DB_INVAL;
    }
    if (Value[0] == 0) {
        return DbDelete(Key);
    }
    Slot = FindSlot(Key);
    if (Slot >= 0) {
        int i = 0;
        while (gRecs[Slot].Val[i] && Value[i] && gRecs[Slot].Val[i] == Value[i]) {
            i++;
        }
        if (gRecs[Slot].Val[i] == 0 && Value[i] == 0) {
            return DB_OK; /* 未变，免写盘 */
        }
    }
    if (Slot < 0) {
        Slot = AllocSlot();
    }
    if (Slot < 0) {
        return DB_FULL;
    }
    gRecs[Slot].Used = 1;
    CopyStr(gRecs[Slot].Key, DB_KEY_MAX, Key);
    CopyStr(gRecs[Slot].Val, DB_VAL_MAX, Value);
    gDbDirty = 1;
    if (gBatch) {
        return DB_OK;
    }
    return DbSave();
}

int DbDelete(const char *Key) {
    int Slot;
    if (!gReady || !KeyOk(Key)) {
        return DB_INVAL;
    }
    Slot = FindSlot(Key);
    if (Slot < 0) {
        return DB_NOENT;
    }
    gRecs[Slot].Used = 0;
    gRecs[Slot].Key[0] = 0;
    gRecs[Slot].Val[0] = 0;
    gDbDirty = 1;
    if (gBatch) {
        return DB_OK;
    }
    return DbSave();
}

void DbBeginBatch(void) {
    gBatch = 1;
}

int DbEndBatch(void) {
    gBatch = 0;
    if (!gDbDirty) {
        return DB_OK;
    }
    return DbSave();
}

int DbCount(void) {
    int i;
    int N = 0;
    for (i = 0; i < DB_MAX_RECORDS; i++) {
        if (gRecs[i].Used) {
            N++;
        }
    }
    return N;
}

int DbForEach(int (*Cb)(const char *Key, const char *Value, void *Ctx), void *Ctx) {
    int i;
    if (!Cb) {
        return DB_INVAL;
    }
    for (i = 0; i < DB_MAX_RECORDS; i++) {
        if (!gRecs[i].Used) {
            continue;
        }
        if (Cb(gRecs[i].Key, gRecs[i].Val, Ctx) != 0) {
            break;
        }
    }
    return DB_OK;
}

static void CommandDbGet(int Argc, char **Argv) {
    char Val[DB_VAL_MAX];
    int Err;

    if (Argc < 2) {
        ConsoleWrite("usage: dbget <key>\n");
        return;
    }
    Err = DbGet(Argv[1], Val, sizeof(Val));
    if (Err == DB_NOENT) {
        ConsoleWrite("dbget: not found\n");
        return;
    }
    if (Err != DB_OK) {
        ConsoleWrite("dbget: error\n");
        return;
    }
    ConsoleWrite(Val);
    ConsoleWrite("\n");
}

static void CommandDbSet(int Argc, char **Argv) {
    static char Val[DB_VAL_MAX];
    UINTN Len = 0;
    int a;
    int first = 1;
    int Err;

    if (Argc < 3) {
        ConsoleWrite("usage: dbset <key> <value...>\n");
        return;
    }
    for (a = 2; a < Argc; a++) {
        const char *S = Argv[a];
        if (!first) {
            if (Len + 1 >= sizeof(Val)) {
                break;
            }
            Val[Len++] = ' ';
        }
        first = 0;
        while (*S && Len + 1 < sizeof(Val)) {
            Val[Len++] = *S++;
        }
    }
    Val[Len] = 0;
    Err = DbSet(Argv[1], Val);
    if (Err == DB_FULL) {
        ConsoleWrite("dbset: full\n");
        return;
    }
    if (Err == DB_INVAL) {
        ConsoleWrite("dbset: invalid key\n");
        return;
    }
    if (Err != DB_OK) {
        ConsoleWrite("dbset: save failed\n");
        return;
    }
    ConsoleWrite("dbset: ok\n");
}

static int ListCb(const char *Key, const char *Value, void *Ctx) {
    (void)Ctx;
    ConsoleWrite(Key);
    ConsoleWrite("=");
    ConsoleWrite(Value);
    ConsoleWrite("\n");
    return 0;
}

static void CommandDbList(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    if (DbCount() == 0) {
        ConsoleWrite("dblist: (empty)\n");
        return;
    }
    (void)DbForEach(ListCb, 0);
}

int DbInit(void) {
    int Err;

    if (gReady) {
        return DB_OK;
    }
    ClearAll();
    Err = DbLoad();
    if (Err == DB_NOENT) {
        ImportThemeCfgIfEmpty();
    }
    gReady = 1;
    ConsoleRegister2("database", "get", "get KV from TOYOS.DB", CommandDbGet);
    ConsoleRegister2("database", "set", "set KV in TOYOS.DB", CommandDbSet);
    ConsoleRegister2("database", "list", "list TOYOS.DB", CommandDbList);
    ConsoleRegisterAliasLine("dbget", "database", "get");
    ConsoleRegisterAliasLine("dbset", "database", "set");
    ConsoleRegisterAliasLine("dblist", "database", "list");
    DebugWrite("db: ready records=");
    DebugHex32((UINT32)DbCount());
    DebugWrite("\n");
    return DB_OK;
}
