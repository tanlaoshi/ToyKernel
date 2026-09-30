/*
 * StoreCatalog.c — catalog 解析与加载（本地 Store/ + 远程 remote.cat 合并）
 */
#include "Store.h"
#include "StorePrivate.h"
#include "FileSystem.h"
#include "Fat.h"
#include "PhysicalMemory.h"

#define STORE_CATALOG_MAX  (8u * 1024u)

static STORE_ENTRY sRemoteTab[STORE_ENTRIES_MAX];

static void CopyTok(char *Dst, int DstMax, const char *Start, const char *End) {
    int N = 0;

    if (!Dst || DstMax <= 0) {
        return;
    }
    while (Start < End && (*Start == ' ' || *Start == '\t')) {
        Start++;
    }
    while (End > Start && (End[-1] == ' ' || End[-1] == '\t' || End[-1] == '\r')) {
        End--;
    }
    while (Start < End && N + 1 < DstMax) {
        Dst[N++] = *Start++;
    }
    Dst[N] = 0;
}

static int ParseLine(STORE_ENTRY *E, const char *Line) {
    const char *P;
    const char *Fields[8];
    const char *Starts[8];
    int N = 0;

    while (*Line == ' ' || *Line == '\t') {
        Line++;
    }
    if (*Line == 0 || *Line == '#') {
        return -1;
    }
    P = Line;
    Starts[0] = P;
    while (*P && N < 8) {
        if (*P == '|') {
            Fields[N] = P;
            N++;
            if (N < 8) {
                Starts[N] = P + 1;
            }
        }
        P++;
    }
    if (N != 6 && N != 7) {
        return -1;
    }
    Fields[N] = P;
    CopyTok(E->Id, STORE_ID_MAX, Starts[0], Fields[0]);
    CopyTok(E->Type, (int)sizeof(E->Type), Starts[1], Fields[1]);
    {
        char Ver[16];
        UINT32 V = 0;
        const char *S;

        CopyTok(Ver, (int)sizeof(Ver), Starts[2], Fields[2]);
        S = Ver;
        while (*S >= '0' && *S <= '9') {
            V = V * 10u + (UINT32)(*S - '0');
            S++;
        }
        E->Version = V;
    }
    CopyTok(E->File, STORE_FILE_MAX, Starts[3], Fields[3]);
    CopyTok(E->Sha256, (int)sizeof(E->Sha256), Starts[4], Fields[4]);
    CopyTok(E->Arch, STORE_ARCH_MAX, Starts[5], Fields[5]);
    CopyTok(E->Title, STORE_TITLE_MAX, Starts[6], Fields[6]);
    E->Depends[0] = 0;
    E->Origin = STORE_SRC_LOCAL;
    if (N == 7) {
        CopyTok(E->Depends, STORE_DEPENDS_MAX, Starts[7], Fields[7]);
        NormalizeDepends(E->Depends);
    }
    if (E->Id[0] == 0 || E->File[0] == 0) {
        return -1;
    }
    if (E->Type[0] == 0) {
        E->Type[0] = 'a';
        E->Type[1] = 'p';
        E->Type[2] = 'p';
        E->Type[3] = 0;
    }
    return 0;
}

const char *StoreHostArch(void) {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__)
    return "arm64";
#elif defined(__riscv)
    return "riscv64";
#else
    return "any";
#endif
}

static int LoadCatalogPath(const char *Path, STORE_ENTRY *Out, int Max, int *OutCount) {
    UINT8 *Buf;
    UINT32 Pages;
    UINTN Size;
    UINTN i;
    UINTN LineStart;
    int Count;
    int Err;

    if (!Out || Max <= 0 || !OutCount) {
        return FAT_ERR_INVAL;
    }
    *OutCount = 0;
    Pages = (STORE_CATALOG_MAX + 4095u) / 4096u;
    Buf = (UINT8 *)PhysicalMemoryAllocatePages(Pages);
    if (!Buf) {
        return FAT_ERR_NOSPC;
    }
    Size = 0;
    Err = FileSystemReadFile(Path, Buf, STORE_CATALOG_MAX - 1, &Size);
    if (Err != FAT_OK || Size == 0) {
        PhysicalMemoryFreePages(Buf, Pages);
        return Err != FAT_OK ? Err : FAT_ERR_NOENT;
    }
    Buf[Size] = 0;
    Count = 0;
    LineStart = 0;
    for (i = 0; i <= Size; i++) {
        if (i == Size || Buf[i] == '\n' || Buf[i] == '\r') {
            char Saved = (char)Buf[i];
            Buf[i] = 0;
            if (i > LineStart && Count < Max) {
                if (ParseLine(&Out[Count], (const char *)&Buf[LineStart]) == 0) {
                    Count++;
                }
            }
            Buf[i] = (UINT8)Saved;
            if (i < Size && Buf[i] == '\r' && i + 1 < Size && Buf[i + 1] == '\n') {
                i++;
            }
            LineStart = i + 1;
        }
    }
    PhysicalMemoryFreePages(Buf, Pages);
    *OutCount = Count;
    return FAT_OK;
}

static void MarkOrigin(STORE_ENTRY *Tab, int N, int Origin) {
    int i;

    for (i = 0; i < N; i++) {
        Tab[i].Origin = Origin;
    }
}

static int FindId(STORE_ENTRY *Tab, int N, const char *Id) {
    int i;

    for (i = 0; i < N; i++) {
        if (StrEq(Tab[i].Id, Id)) {
            return i;
        }
    }
    return -1;
}

int StoreLoadCatalog(STORE_ENTRY *Out, int Max, int *OutCount) {
    int Err;
    int LocalN = 0;
    int RemoteN = 0;
    int Count;
    int i;
    int Hit;
    static const char Builtin[] =
        "hello|app|1|HELLO.ELF|-|x86_64|Hello\n"
        "guidemo|app|1|GUIDEMO.ELF|-|x86_64|GUI Demo|demopack,sun8\n"
        "cat|app|1|CAT.ELF|-|x86_64|Cat\n"
        "taskmgr|app|1|TASKMGR.ELF|-|x86_64|Task Manager\n"
        "sun8|font|1|VGA8X16.FNT|-|any|Sun 8x16 (store)\n"
        "demopack|asset|1|INFO.TXT|-|any|Demo asset pack\n";
    const char *P;
    char LineBuf[192];
    int Li;

    if (!Out || Max <= 0 || !OutCount) {
        return FAT_ERR_INVAL;
    }
    *OutCount = 0;
    Count = 0;
    Err = LoadCatalogPath(STORE_CATALOG_PATH, Out, Max, &LocalN);
    if (Err == FAT_OK && LocalN > 0) {
        MarkOrigin(Out, LocalN, STORE_SRC_LOCAL);
        Count = LocalN;
    }
    Err = LoadCatalogPath(STORE_REMOTE_CAT, sRemoteTab, Max, &RemoteN);
    if (Err == FAT_OK && RemoteN > 0) {
        MarkOrigin(sRemoteTab, RemoteN, STORE_SRC_NET);
        for (i = 0; i < RemoteN; i++) {
            Hit = FindId(Out, Count, sRemoteTab[i].Id);
            if (Hit >= 0) {
                Out[Hit] = sRemoteTab[i];
            } else if (Count < Max) {
                Out[Count++] = sRemoteTab[i];
            }
        }
    }
    if (Count > 0) {
        *OutCount = Count;
        return Count;
    }

    Count = 0;
    P = Builtin;
    while (*P && Count < Max) {
        Li = 0;
        while (*P && *P != '\n' && Li + 1 < (int)sizeof(LineBuf)) {
            LineBuf[Li++] = *P++;
        }
        LineBuf[Li] = 0;
        if (*P == '\n') {
            P++;
        }
        if (ParseLine(&Out[Count], LineBuf) == 0) {
            Out[Count].Origin = STORE_SRC_LOCAL;
            Count++;
        }
    }
    *OutCount = Count;
    return Count > 0 ? Count : FAT_ERR_NOENT;
}
