/*
 * Store.c — PR-S1：离线 catalog 安装到 Apps/（无网）
 *
 * 载荷查找顺序：Store/<file> → <file>（卷根）→ Assets/Store/packages/<id>/<file>
 * sha256=- 时跳过校验（教学默认）。
 */
#include "Store.h"
#include "FileSystem.h"
#include "Fat.h"
#include "PhysicalMemory.h"
#include "HalConsole.h"

#define STORE_CATALOG_MAX  (8u * 1024u)
#define STORE_COPY_MAX     FAT_WRITE_MAX

static int StrEq(const char *A, const char *B) {
    if (!A || !B) {
        return 0;
    }
    while (*A && *A == *B) {
        A++;
        B++;
    }
    return *A == 0 && *B == 0;
}

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
    const char *Fields[7];
    const char *Starts[7];
    int N = 0;
    int i;

    while (*Line == ' ' || *Line == '\t') {
        Line++;
    }
    if (*Line == 0 || *Line == '#') {
        return -1;
    }
    P = Line;
    Starts[0] = P;
    while (*P && N < 7) {
        if (*P == '|') {
            Fields[N] = P;
            N++;
            if (N < 7) {
                Starts[N] = P + 1;
            }
        }
        P++;
    }
    if (N != 6) {
        return -1; /* 需 7 段 → 6 个 | */
    }
    Fields[6] = P;
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
    if (E->Id[0] == 0 || E->File[0] == 0) {
        return -1;
    }
    if (E->Type[0] == 0) {
        E->Type[0] = 'a';
        E->Type[1] = 'p';
        E->Type[2] = 'p';
        E->Type[3] = 0;
    }
    (void)i;
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

static int ArchOk(const char *Arch) {
    const char *Host;

    if (!Arch || Arch[0] == 0 || StrEq(Arch, "any")) {
        return 1;
    }
    Host = StoreHostArch();
    return StrEq(Arch, Host);
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

    Err = LoadCatalogPath(STORE_CATALOG_PATH, Out, Max, OutCount);
    if (Err == FAT_OK && *OutCount > 0) {
        return *OutCount;
    }
    Err = LoadCatalogPath(STORE_CATALOG_ALT, Out, Max, OutCount);
    if (Err == FAT_OK) {
        return *OutCount;
    }
    return Err;
}

static void JoinPath(char *Dst, int DstMax, const char *A, const char *B) {
    int i = 0;
    int j = 0;

    if (!Dst || DstMax <= 0) {
        return;
    }
    while (A && A[i] && i + 1 < DstMax) {
        Dst[i] = A[i];
        i++;
    }
    if (i > 0 && Dst[i - 1] != '/' && i + 1 < DstMax) {
        Dst[i++] = '/';
    }
    while (B && B[j] && i + 1 < DstMax) {
        Dst[i++] = B[j++];
    }
    Dst[i] = 0;
}

static int TryCopy(const char *Src, const char *Dst) {
    FAT_FILE_STAT St;
    UINT8 *Buf;
    UINT32 Pages;
    UINTN Size;
    UINTN Got;
    int Err;

    if (FileSystemFileStat(Src, &St) != FAT_OK || (St.Attr & FAT_ATTR_DIR)) {
        return FAT_ERR_NOENT;
    }
    Size = St.Size;
    if (Size == 0 || Size > STORE_COPY_MAX) {
        return FAT_ERR_FBIG;
    }
    Pages = (UINT32)((Size + 4095u) / 4096u);
    if (Pages == 0) {
        Pages = 1;
    }
    Buf = (UINT8 *)PhysicalMemoryAllocatePages(Pages);
    if (!Buf) {
        return FAT_ERR_NOSPC;
    }
    Got = 0;
    Err = FileSystemReadFile(Src, Buf, Size, &Got);
    if (Err != FAT_OK || Got != Size) {
        PhysicalMemoryFreePages(Buf, Pages);
        return Err != FAT_OK ? Err : FAT_ERR_IO;
    }
    Err = FileSystemWriteFile(Dst, Buf, Got);
    PhysicalMemoryFreePages(Buf, Pages);
    return Err;
}

static int EnsureAppsDir(void) {
    int Err = FileSystemMakeDirectory(STORE_APPS_DIR);
    if (Err == FAT_OK || Err == FAT_ERR_EXIST) {
        return FAT_OK;
    }
    return Err;
}

int StoreInstall(const char *Id) {
    STORE_ENTRY Tab[STORE_ENTRIES_MAX];
    int Count = 0;
    int i;
    int Err;
    char Src[128];
    char Dst[96];
    char Pkg[160];

    if (!Id || Id[0] == 0) {
        return FAT_ERR_INVAL;
    }
    Err = StoreLoadCatalog(Tab, STORE_ENTRIES_MAX, &Count);
    if (Err < 0) {
        return Err;
    }
    for (i = 0; i < Count; i++) {
        if (!StrEq(Tab[i].Id, Id)) {
            continue;
        }
        if (!StrEq(Tab[i].Type, "app")) {
            HalConsoleWriteSerial("store: only type=app in S1\n");
            return FAT_ERR_INVAL;
        }
        if (!ArchOk(Tab[i].Arch)) {
            HalConsoleWriteSerial("store: arch mismatch\n");
            return FAT_ERR_INVAL;
        }
        if (Tab[i].Sha256[0] && Tab[i].Sha256[0] != '-') {
            /* S1：有哈希也先跳过（无 SHA 实现）；S2 再验 */
        }
        Err = EnsureAppsDir();
        if (Err != FAT_OK) {
            return Err;
        }
        JoinPath(Dst, (int)sizeof(Dst), STORE_APPS_DIR, Tab[i].File);

        JoinPath(Src, (int)sizeof(Src), "Store", Tab[i].File);
        Err = TryCopy(Src, Dst);
        if (Err == FAT_OK) {
            return FAT_OK;
        }
        Err = TryCopy(Tab[i].File, Dst);
        if (Err == FAT_OK) {
            return FAT_OK;
        }
        /* Assets/Store/packages/<id>/<file> */
        JoinPath(Pkg, (int)sizeof(Pkg), "Assets/Store/packages", Tab[i].Id);
        JoinPath(Src, (int)sizeof(Src), Pkg, Tab[i].File);
        Err = TryCopy(Src, Dst);
        if (Err == FAT_OK) {
            return FAT_OK;
        }
        return FAT_ERR_NOENT;
    }
    return FAT_ERR_NOENT;
}
