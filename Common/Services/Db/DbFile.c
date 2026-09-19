/*
 * DbFile.c — TOYOS.DB 读盘 / 写盘（PR-S-db-1）
 */
#include "DbPrivate.h"
#include "FileSystem.h"
#include "Hal.h"
#include "HalConsole.h"
#include "Theme.h"

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
    if (FileSystemReadFile(DB_PATH, Buf, sizeof(Buf) - 1, &Size) != FAT_OK || Size == 0) {
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
    gDbDirty = 0;
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
    /* 同名覆盖写出（勿先 Delete：vvfat unlink+create 易丢文件） */
    if (FileSystemWriteFile(DB_PATH, Buf, N) != FAT_OK) {
        return DB_ERR;
    }
    gDbDirty = 0;
    return DB_OK;
}

/*
 * 若库空且存在 THEME.CFG，导入 desktop/shell/font/mode（不覆盖已有键）。
 */
void ImportThemeCfgIfEmpty(void) {
    static char Buf[256];
    UINTN Size = 0;
    UINTN i;
    char Line[80];
    UINTN L;

    if (DbCount() > 0) {
        return;
    }
    if (FileSystemReadFile(THEME_CFG_PATH, Buf, sizeof(Buf) - 1, &Size) != FAT_OK ||
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
