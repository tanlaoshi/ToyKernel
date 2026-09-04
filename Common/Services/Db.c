/*
 * Db.c — TOYOS.DB 文本 KV（PR-DB1）
 */
#include "Db.h"
#include "FileSystem.h"
#include "Console.h"
#include "Hal.h"
#include "Debug.h"
#include "Theme.h"

typedef struct {
    char Key[DB_KEY_MAX];
    char Val[DB_VAL_MAX];
    int  Used;
} DB_REC;

static DB_REC gRecs[DB_MAX_RECORDS];
static int gReady;
static int gDirty;

static int IsSpace(char C) {
    return C == ' ' || C == '\t' || C == '\r' || C == '\n';
}

static int KeyOk(const char *K) {
    int N = 0;
    if (!K || !K[0]) {
        return 0;
    }
    while (K[N] && N < DB_KEY_MAX - 1) {
        char C = K[N];
        int Ok = (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') ||
                 (C >= '0' && C <= '9') || C == '_' || C == '.' || C == '-';
        if (!Ok) {
            return 0;
        }
        N++;
    }
    return K[N] == 0;
}

static void CopyStr(char *Dst, int Max, const char *Src) {
    int i;
    if (Max <= 0) {
        return;
    }
    for (i = 0; Src && Src[i] && i < Max - 1; i++) {
        Dst[i] = Src[i];
    }
    Dst[i] = 0;
}

static int StrEq(const char *A, const char *B) {
    while (*A && *B) {
        if (*A != *B) {
            return 0;
        }
        A++;
        B++;
    }
    return *A == 0 && *B == 0;
}

static int FindSlot(const char *Key) {
    int i;
    for (i = 0; i < DB_MAX_RECORDS; i++) {
        if (gRecs[i].Used && StrEq(gRecs[i].Key, Key)) {
            return i;
        }
    }
    return -1;
}

static int AllocSlot(void) {
    int i;
    for (i = 0; i < DB_MAX_RECORDS; i++) {
        if (!gRecs[i].Used) {
            return i;
        }
    }
    return -1;
}

static void ClearAll(void) {
    int i;
    for (i = 0; i < DB_MAX_RECORDS; i++) {
        gRecs[i].Used = 0;
        gRecs[i].Key[0] = 0;
        gRecs[i].Val[0] = 0;
    }
    gDirty = 0;
}

static void ApplyLine(const char *Line) {
    char Key[DB_KEY_MAX];
    char Val[DB_VAL_MAX];
    int Ki = 0;
    int Vi = 0;
    const char *P = Line;
    int Slot;

    while (*P && IsSpace(*P)) {
        P++;
    }
    if (!*P || *P == '#') {
        return;
    }
    while (*P && *P != '=' && !IsSpace(*P) && Ki < DB_KEY_MAX - 1) {
        Key[Ki++] = *P++;
    }
    Key[Ki] = 0;
    while (*P && IsSpace(*P)) {
        P++;
    }
    if (*P != '=') {
        return;
    }
    P++;
    while (*P && IsSpace(*P)) {
        P++;
    }
    while (*P && *P != '\n' && *P != '\r' && Vi < DB_VAL_MAX - 1) {
        Val[Vi++] = *P++;
    }
    while (Vi > 0 && IsSpace(Val[Vi - 1])) {
        Vi--;
    }
    Val[Vi] = 0;
    if (!KeyOk(Key)) {
        return;
    }
    Slot = FindSlot(Key);
    if (Slot < 0) {
        Slot = AllocSlot();
    }
    if (Slot < 0) {
        return;
    }
    gRecs[Slot].Used = 1;
    CopyStr(gRecs[Slot].Key, DB_KEY_MAX, Key);
    CopyStr(gRecs[Slot].Val, DB_VAL_MAX, Val);
}

int DbLoad(void) {
    static char Buf[4096];
    UINTN Size = 0;
    UINTN i;
    char Line[DB_KEY_MAX + DB_VAL_MAX + 4];
    UINTN L;

    ClearAll();
    if (FsReadFile(DB_PATH, Buf, sizeof(Buf) - 1, &Size) != FAT_OK || Size == 0) {
        return DB_NOENT;
    }
    Buf[Size] = 0;
    L = 0;
    for (i = 0; i <= Size; i++) {
        char C = (i < Size) ? Buf[i] : '\n';
        if (C == '\n' || C == '\r' || i == Size) {
            if (L > 0) {
                Line[L] = 0;
                ApplyLine(Line);
                L = 0;
            }
            continue;
        }
        if (L + 1 < sizeof(Line)) {
            Line[L++] = C;
        }
    }
    gDirty = 0;
    return DB_OK;
}

int DbSave(void) {
    static char Buf[4096];
    UINTN N = 0;
    int i;
    int k;

    Buf[N++] = '#';
    Buf[N++] = ' ';
    Buf[N++] = 'T';
    Buf[N++] = 'O';
    Buf[N++] = 'Y';
    Buf[N++] = 'O';
    Buf[N++] = 'S';
    Buf[N++] = '.';
    Buf[N++] = 'D';
    Buf[N++] = 'B';
    Buf[N++] = '\n';

    for (i = 0; i < DB_MAX_RECORDS; i++) {
        if (!gRecs[i].Used) {
            continue;
        }
        for (k = 0; gRecs[i].Key[k] && N + 2 < sizeof(Buf); k++) {
            Buf[N++] = gRecs[i].Key[k];
        }
        if (N + 1 >= sizeof(Buf)) {
            break;
        }
        Buf[N++] = '=';
        for (k = 0; gRecs[i].Val[k] && N + 2 < sizeof(Buf); k++) {
            Buf[N++] = gRecs[i].Val[k];
        }
        if (N + 1 >= sizeof(Buf)) {
            break;
        }
        Buf[N++] = '\n';
    }
    if (FsWriteFile(DB_PATH, Buf, N) != FAT_OK) {
        return DB_ERR;
    }
    gDirty = 0;
    return DB_OK;
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
    if (Slot < 0) {
        Slot = AllocSlot();
    }
    if (Slot < 0) {
        return DB_FULL;
    }
    gRecs[Slot].Used = 1;
    CopyStr(gRecs[Slot].Key, DB_KEY_MAX, Key);
    CopyStr(gRecs[Slot].Val, DB_VAL_MAX, Value);
    gDirty = 1;
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
    gDirty = 1;
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

/*
 * 若库空且存在 THEME.CFG，导入 desktop/shell/font/mode（不覆盖已有键）。
 */
static void ImportThemeCfgIfEmpty(void) {
    static char Buf[256];
    UINTN Size = 0;
    UINTN i;
    char Line[80];
    UINTN L;

    if (DbCount() > 0) {
        return;
    }
    if (FsReadFile(THEME_CFG_PATH, Buf, sizeof(Buf) - 1, &Size) != FAT_OK ||
        Size == 0) {
        return;
    }
    Buf[Size] = 0;
    L = 0;
    for (i = 0; i <= Size; i++) {
        char C = (i < Size) ? Buf[i] : '\n';
        if (C == '\n' || C == '\r' || i == Size) {
            if (L > 0) {
                Line[L] = 0;
                ApplyLine(Line);
                L = 0;
            }
            continue;
        }
        if (L + 1 < sizeof(Line)) {
            Line[L++] = C;
        }
    }
    if (DbCount() == 0) {
        return;
    }
    /* ApplyLine 只填内存；刷盘 */
    (void)DbSave();
    HalConsoleWriteSerial("db: imported THEME.CFG -> TOYOS.DB\n");
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
    ConsoleRegister("dbget", "get KV from TOYOS.DB", CommandDbGet);
    ConsoleRegister("dbset", "set KV in TOYOS.DB", CommandDbSet);
    ConsoleRegister("dblist", "list TOYOS.DB", CommandDbList);
    DebugWrite("db: ready records=");
    DebugHex32((UINT32)DbCount());
    DebugWrite("\n");
    return DB_OK;
}
