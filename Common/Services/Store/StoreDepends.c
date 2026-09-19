/*
 * StoreDepends.c — PKG 依赖解析与已装检查
 * 核心：Store.c
 */
#include "Store.h"
#include "StorePrivate.h"
#include "FileSystem.h"
#include "Fat.h"
#include "HalConsole.h"

#define STORE_PKG_MAX      1024u

/*
 * PR-M1：读 packages/<id>/PKG.TXT 的 depends=；有则覆盖 catalog 段。
 * 成功写入 Out 返回 1；无文件/无键返回 0。
 */
int LoadPkgDepends(const char *Id, char *Out, int OutMax) {
    char Path[96];
    char Pkg[80];
    UINT8 Buf[STORE_PKG_MAX];
    UINTN Size = 0;
    UINTN i;
    UINTN LineStart;
    int Err;

    if (!Id || !Out || OutMax <= 0) {
        return 0;
    }
    Out[0] = 0;
    JoinPath(Pkg, (int)sizeof(Pkg), "Assets/Store/packages", Id);
    JoinPath(Path, (int)sizeof(Path), Pkg, "PKG.TXT");
    Err = FileSystemReadFile(Path, Buf, STORE_PKG_MAX - 1, &Size);
    if (Err != FAT_OK || Size == 0) {
        return 0;
    }
    Buf[Size] = 0;
    LineStart = 0;
    for (i = 0; i <= Size; i++) {
        if (i == Size || Buf[i] == '\n' || Buf[i] == '\r') {
            char Saved = (char)Buf[i];
            const char *L;
            Buf[i] = 0;
            L = (const char *)&Buf[LineStart];
            while (*L == ' ' || *L == '\t') {
                L++;
            }
            if (L[0] == 'd' && L[1] == 'e' && L[2] == 'p' && L[3] == 'e' &&
                L[4] == 'n' && L[5] == 'd' && L[6] == 's' && L[7] == '=') {
                CopyStr(Out, OutMax, L + 8);
                NormalizeDepends(Out);
                Buf[i] = (UINT8)Saved;
                return 1;
            }
            Buf[i] = (UINT8)Saved;
            if (i < Size && Buf[i] == '\r' && i + 1 < Size && Buf[i + 1] == '\n') {
                i++;
            }
            LineStart = i + 1;
        }
    }
    return 0;
}

/* 缺依赖 → 串口提示并返回 FAT_ERR_INVAL（不静默强装；M1 / 单包 install） */
int CheckDependsInstalled(const char *Depends) {
    char Tok[STORE_ID_MAX];
    const char *P;
    int Missing = 0;
    int n;

    if (!Depends || Depends[0] == 0) {
        return FAT_OK;
    }
    P = Depends;
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
        if (!StoreIsInstalled(Tok)) {
            if (!Missing) {
                HalConsoleWriteSerial("store: missing depends:");
            }
            HalConsoleWriteSerial(" ");
            HalConsoleWriteSerial(Tok);
            Missing = 1;
        }
    }
    if (Missing) {
        HalConsoleWriteSerial("\n");
        HalConsoleWriteSerial("hint: store install <dep> first, or store combo <id>\n");
        return FAT_ERR_INVAL;
    }
    return FAT_OK;
}

/* Depends 串是否含 Id（逗号分隔） */
static int DependsHasId(const char *Depends, const char *Id) {
    char Tok[STORE_ID_MAX];
    const char *P;
    int n;

    if (!Depends || !Id || Id[0] == 0 || Depends[0] == 0 ||
        (Depends[0] == '-' && Depends[1] == 0)) {
        return 0;
    }
    P = Depends;
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
        if (Tok[0] && StrEq(Tok, Id)) {
            return 1;
        }
    }
    return 0;
}

/* catalog + PKG 覆盖 → OutDepends（已 Normalize） */
int ResolveEntryDepends(const char *Id, char *OutDepends, int OutMax) {
    STORE_ENTRY *Tab = gStoreTab;
    int Count = 0;
    int i;
    int Err;

    if (!Id || !OutDepends || OutMax <= 0) {
        return FAT_ERR_INVAL;
    }
    OutDepends[0] = 0;
    Err = StoreLoadCatalog(Tab, STORE_ENTRIES_MAX, &Count);
    if (Err < 0) {
        return Err;
    }
    for (i = 0; i < Count; i++) {
        if (!StrEq(Tab[i].Id, Id)) {
            continue;
        }
        CopyStr(OutDepends, OutMax, Tab[i].Depends);
        if (LoadPkgDepends(Tab[i].Id, OutDepends, OutMax)) {
            /* PKG 覆盖 */
        }
        NormalizeDepends(OutDepends);
        return FAT_OK;
    }
    return FAT_ERR_NOENT;
}

/* 已装包中谁依赖 Id → OutIds；返回数量 */
int CollectDependents(const char *Id, char OutIds[][STORE_ID_MAX], int Max) {
    STORE_INSTALLED Inst[STORE_INSTALLED_MAX];
    int N = 0;
    int i;
    int OutN = 0;
    char Dep[STORE_DEPENDS_MAX];

    if (!Id || !OutIds || Max <= 0) {
        return 0;
    }
    if (StoreListInstalled(Inst, STORE_INSTALLED_MAX, &N) != FAT_OK) {
        return 0;
    }
    for (i = 0; i < N && OutN < Max; i++) {
        if (StrEq(Inst[i].Id, Id)) {
            continue;
        }
        Dep[0] = 0;
        (void)StoreGetDepends(Inst[i].Id, Dep, (int)sizeof(Dep));
        if (DependsHasId(Dep, Id)) {
            CopyStr(OutIds[OutN], STORE_ID_MAX, Inst[i].Id);
            OutN++;
        }
    }
    return OutN;
}
