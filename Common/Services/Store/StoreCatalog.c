/*
 * StoreCatalog.c — catalog 解析与加载
 * 核心：Store.c。安装与组合包仍在 Store.c。
 */
#include "Store.h"
#include "StorePriv.h"
#include "FileSystem.h"
#include "Fat.h"
#include "PhysicalMemory.h"

#define STORE_CATALOG_MAX  (8u * 1024u)

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
    /* 7 段（6 个 |）或 8 段含 depends（7 个 |） */
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

int StoreLoadCatalog(STORE_ENTRY *Out, int Max, int *OutCount) {
    int Err;
    static const char Builtin[] =
        "hello|app|1|HELLO.ELF|-|x86_64|Hello\n"
        "guidemo|app|1|GUIDEMO.ELF|-|x86_64|GUI Demo|demopack,sun8\n"
        "cat|app|1|CAT.ELF|-|x86_64|Cat\n"
        "sun8|font|1|VGA8X16.FNT|-|any|Sun 8x16 (store)\n"
        "demopack|asset|1|INFO.TXT|-|any|Demo asset pack\n";
    const char *P;
    const char *Line;
    char LineBuf[192];
    int Li;
    int Count;

    /* PR-S2：已 sync 的 Store/catalog.txt 优先覆盖镜像内 Assets */
    Err = LoadCatalogPath(STORE_CATALOG_ALT, Out, Max, OutCount);
    if (Err == FAT_OK && *OutCount > 0) {
        return *OutCount;
    }
    Err = LoadCatalogPath(STORE_CATALOG_PATH, Out, Max, OutCount);
    if (Err == FAT_OK && *OutCount > 0) {
        return *OutCount;
    }

    /* 无盘/空 catalog：内核内置离线表（不必搭服务器） */
    if (!Out || Max <= 0 || !OutCount) {
        return FAT_ERR_INVAL;
    }
    Count = 0;
    P = Builtin;
    while (*P && Count < Max) {
        Line = P;
        Li = 0;
        while (*P && *P != '\n' && Li + 1 < (int)sizeof(LineBuf)) {
            LineBuf[Li++] = *P++;
        }
        LineBuf[Li] = 0;
        if (*P == '\n') {
            P++;
        }
        (void)Line;
        if (ParseLine(&Out[Count], LineBuf) == 0) {
            Count++;
        }
    }
    *OutCount = Count;
    return Count > 0 ? Count : FAT_ERR_NOENT;
}
