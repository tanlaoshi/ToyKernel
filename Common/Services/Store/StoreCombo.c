/*
 * StoreCombo.c — 按依赖顺序装卸多包
 * 核心：Store.c。单包安装在 StoreInstall.c。
 * PR-S-job-phases：Plan + Batch 供 StoreJob 按包切片。
 */
#include "Store.h"
#include "StorePrivate.h"
#include "Fat.h"
#include "HalConsole.h"
#include "Hal.h"

/* 字体/资源包/库：可被多 app 引用，uncombo 只卸叶子 app，不级联卸这些 */
static int PackageIsSharedDep(int Kind) {
    return Kind == STORE_KIND_FONT ||
           Kind == STORE_KIND_ASSET ||
           Kind == STORE_KIND_LIB ||
           Kind < 0; /* 未知类型宁可保留 */
}

static int PlanHasId(char Out[][STORE_ID_MAX], int N, const char *Id) {
    int i;

    for (i = 0; i < N; i++) {
        if (StrEq(Out[i], Id)) {
            return 1;
        }
    }
    return 0;
}

static int PlanInstallRec(const char *Id, int Depth,
                          char Out[][STORE_ID_MAX], int Max, int *OutN) {
    char DepBuf[STORE_DEPENDS_MAX];
    char Tok[STORE_ID_MAX];
    const char *P;
    int n;
    int Err;

    if (!Id || Id[0] == 0 || !Out || !OutN) {
        return FAT_ERR_INVAL;
    }
    if (Depth > STORE_ENTRIES_MAX) {
        HalConsoleWriteSerial("store combo: depends cycle or too deep\n");
        return FAT_ERR_INVAL;
    }
    if (StoreIsInstalled(Id)) {
        if (!StoreHasSi(Id)) {
            Err = StoreAdoptInstalled(Id);
            if (Err != FAT_OK) {
                return Err;
            }
        }
        return FAT_OK;
    }
    if (PlanHasId(Out, *OutN, Id)) {
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
        Err = PlanInstallRec(Tok, Depth + 1, Out, Max, OutN);
        if (Err != FAT_OK) {
            return Err;
        }
    }

    if (*OutN >= Max) {
        return FAT_ERR_INVAL;
    }
    CopyStr(Out[*OutN], STORE_ID_MAX, Id);
    (*OutN)++;
    return FAT_OK;
}

int StoreComboPlanInstall(const char *Id, char OutIds[][STORE_ID_MAX], int Max,
                          int *OutN) {
    if (!OutN) {
        return FAT_ERR_INVAL;
    }
    *OutN = 0;
    if (!Id || Id[0] == 0 || !OutIds || Max <= 0) {
        return FAT_ERR_INVAL;
    }
    return PlanInstallRec(Id, 0, OutIds, Max, OutN);
}

int StoreComboPlanRemove(const char *Id, char OutIds[][STORE_ID_MAX], int Max,
                         int *OutN) {
    char DepBuf[STORE_DEPENDS_MAX];
    char Tok[STORE_ID_MAX];
    char Deps[STORE_ENTRIES_MAX][STORE_ID_MAX];
    const char *P;
    int DepN = 0;
    int n;
    int i;

    if (!OutN) {
        return FAT_ERR_INVAL;
    }
    *OutN = 0;
    if (!Id || Id[0] == 0 || !OutIds || Max <= 0) {
        return FAT_ERR_INVAL;
    }
    if (!StoreIsInstalled(Id)) {
        return FAT_ERR_NOENT;
    }

    DepBuf[0] = 0;
    (void)StoreGetDepends(Id, DepBuf, (int)sizeof(DepBuf));
    NormalizeDepends(DepBuf);
    if (DepBuf[0] == 0) {
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

    CopyStr(OutIds[0], STORE_ID_MAX, Id);
    *OutN = 1;

    for (i = DepN - 1; i >= 0 && *OutN < Max; i--) {
        int Kind;

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
        /* Users 在卸叶子后才准；Job/Combo 逐步 StoreRemove，仍被引用则跳过 */
        CopyStr(OutIds[*OutN], STORE_ID_MAX, Deps[i]);
        (*OutN)++;
    }
    return FAT_OK;
}

void StoreComboBatchBegin(void) {
    gStoreComboDepth++;
}

void StoreComboBatchEnd(void) {
    if (gStoreComboDepth > 0) {
        gStoreComboDepth--;
    }
    if (gStoreComboDepth == 0) {
        StoreFlushFontReload();
    }
}

int StoreComboInstall(const char *Id) {
    char Plan[STORE_ENTRIES_MAX][STORE_ID_MAX];
    int N = 0;
    int i;
    int Err;

    if (!Id || Id[0] == 0) {
        return FAT_ERR_INVAL;
    }
    StoreComboBatchBegin();
    Err = StoreComboPlanInstall(Id, Plan, STORE_ENTRIES_MAX, &N);
    for (i = 0; Err == FAT_OK && i < N; i++) {
        HalConsoleWriteSerial("store combo: +");
        HalConsoleWriteSerial(Plan[i]);
        HalConsoleWriteSerial("\n");
        StoreIoBreath();
        Err = StoreInstall(Plan[i]);
    }
    StoreComboBatchEnd();
    return Err;
}

int StoreComboRemove(const char *Id) {
    char Plan[STORE_ENTRIES_MAX][STORE_ID_MAX];
    int N = 0;
    int i;
    int Err;

    if (!Id || Id[0] == 0) {
        return FAT_ERR_INVAL;
    }
    Err = StoreComboPlanRemove(Id, Plan, STORE_ENTRIES_MAX, &N);
    if (Err != FAT_OK) {
        return Err;
    }
    StoreComboBatchBegin();
    for (i = 0; i < N; i++) {
        HalConsoleWriteSerial("store uncombo: -");
        HalConsoleWriteSerial(Plan[i]);
        HalConsoleWriteSerial("\n");
        StoreIoBreath();
        Err = StoreRemove(Plan[i]);
        if (i > 0 && (Err == FAT_ERR_INVAL || Err == FAT_ERR_NOENT)) {
            /* 仍被引用或已不在：与旧 uncombo continue 一致 */
            continue;
        }
        if (Err != FAT_OK) {
            StoreComboBatchEnd();
            return Err;
        }
    }
    StoreComboBatchEnd();
    return FAT_OK;
}
