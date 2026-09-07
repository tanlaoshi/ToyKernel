/*
 * Store.c — PR-S1：离线 catalog 安装；PR-S3：font/asset → Assets/
 *           PR-S4：ToyDB 已装清单 + store remove
 *
 * 载荷查找顺序：Store/<file> → <file>（卷根）→ Assets/Store/packages/<id>/<file>
 * sha256=- 时跳过校验（教学默认）。
 * 清单键：si.<id>=type|file ；依赖占位 sd.<id>=-（M1 再填）
 */
#include "Store.h"
#include "FileSystem.h"
#include "Fat.h"
#include "PhysicalMemory.h"
#include "HalConsole.h"
#include "Font.h"
#include "Theme.h"
#include "Db.h"

#define STORE_CATALOG_MAX  (8u * 1024u)
#define STORE_COPY_MAX     FAT_WRITE_MAX
#define STORE_KIND_APP     0
#define STORE_KIND_FONT    1
#define STORE_KIND_ASSET   2
#define STORE_CHECK_NONE   0
#define STORE_CHECK_ELF    1
#define STORE_CHECK_TOYF   2

/* 内核任务栈仅 8KiB；catalog 表放 BSS，避免 store sync/HTTP 栈溢出闪退 */
static STORE_ENTRY gStoreTab[STORE_ENTRIES_MAX];

STORE_ENTRY *StoreScratchTab(void) {
    return gStoreTab;
}

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

    /* PR-S2：已 sync 的 Store/catalog.txt 优先覆盖镜像内 Assets */
    Err = LoadCatalogPath(STORE_CATALOG_ALT, Out, Max, OutCount);
    if (Err == FAT_OK && *OutCount > 0) {
        return *OutCount;
    }
    Err = LoadCatalogPath(STORE_CATALOG_PATH, Out, Max, OutCount);
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

static int EnsureDir(const char *Path) {
    int Err = FileSystemMakeDirectory(Path);
    if (Err == FAT_OK || Err == FAT_ERR_EXIST) {
        return FAT_OK;
    }
    return Err;
}

static int EnsureAppsDir(void) {
    return EnsureDir(STORE_APPS_DIR);
}

static int EnsureFontsDir(void) {
    int Err = EnsureDir("Assets");
    if (Err != FAT_OK) {
        return Err;
    }
    return EnsureDir(STORE_FONTS_DIR);
}

static int EnsurePacksDir(void) {
    int Err = EnsureDir("Assets");
    if (Err != FAT_OK) {
        return Err;
    }
    return EnsureDir(STORE_PACKS_DIR);
}

static int EntryKind(const char *Type) {
    if (StrEq(Type, "font")) {
        return STORE_KIND_FONT;
    }
    if (StrEq(Type, "asset")) {
        return STORE_KIND_ASSET;
    }
    if (StrEq(Type, "app") || Type[0] == 0) {
        return STORE_KIND_APP;
    }
    return -1;
}

/*
 * Check: ELF / TOYF / 任意 blob（≥4 字节）。
 * QEMU vvfat：同名覆盖写易坏，先删再建。
 */
static int TryCopy(const char *Src, const char *Dst, int Check) {
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
    if (Size < 4 || Size > STORE_COPY_MAX) {
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
    if (Check == STORE_CHECK_ELF) {
        if (!(Buf[0] == 0x7F && Buf[1] == 'E' && Buf[2] == 'L' && Buf[3] == 'F')) {
            PhysicalMemoryFreePages(Buf, Pages);
            return FAT_ERR_INVAL;
        }
    } else if (Check == STORE_CHECK_TOYF) {
        if (!(Buf[0] == 'T' && Buf[1] == 'O' && Buf[2] == 'Y' && Buf[3] == 'F')) {
            PhysicalMemoryFreePages(Buf, Pages);
            return FAT_ERR_INVAL;
        }
    }
    (void)FileSystemDeleteFile(Dst);
    Err = FileSystemWriteFile(Dst, Buf, Got);
    PhysicalMemoryFreePages(Buf, Pages);
    return Err;
}

static int InstallFromSources(const char *Id, const char *File, const char *Dst,
                              int Check) {
    char Src[128];
    char Pkg[160];
    int Err;

    JoinPath(Src, (int)sizeof(Src), "Store", File);
    Err = TryCopy(Src, Dst, Check);
    if (Err == FAT_OK) {
        return FAT_OK;
    }
    Err = TryCopy(File, Dst, Check);
    if (Err == FAT_OK) {
        return FAT_OK;
    }
    JoinPath(Pkg, (int)sizeof(Pkg), "Assets/Store/packages", Id);
    JoinPath(Src, (int)sizeof(Src), Pkg, File);
    return TryCopy(Src, Dst, Check);
}

static int StoreMarkInstalled(const char *Id, const char *Type, const char *File);

int StoreInstall(const char *Id) {
    STORE_ENTRY *Tab = gStoreTab;
    int Count = 0;
    int i;
    int Err;
    int Kind;
    int Check;
    char Dst[96];

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
        Kind = EntryKind(Tab[i].Type);
        if (Kind < 0) {
            HalConsoleWriteSerial("store: bad type (app|font|asset)\n");
            return FAT_ERR_INVAL;
        }
        if (!ArchOk(Tab[i].Arch)) {
            HalConsoleWriteSerial("store: arch mismatch\n");
            return FAT_ERR_INVAL;
        }

        if (Kind == STORE_KIND_APP) {
            Err = EnsureAppsDir();
            if (Err != FAT_OK) {
                return Err;
            }
            JoinPath(Dst, (int)sizeof(Dst), STORE_APPS_DIR, Tab[i].File);
            Check = STORE_CHECK_ELF;
        } else if (Kind == STORE_KIND_FONT) {
            Err = EnsureFontsDir();
            if (Err != FAT_OK) {
                return Err;
            }
            JoinPath(Dst, (int)sizeof(Dst), STORE_FONTS_DIR, Tab[i].File);
            Check = STORE_CHECK_TOYF;
        } else {
            Err = EnsurePacksDir();
            if (Err != FAT_OK) {
                return Err;
            }
            JoinPath(Dst, (int)sizeof(Dst), STORE_PACKS_DIR, Tab[i].File);
            Check = STORE_CHECK_NONE;
        }

        Err = InstallFromSources(Tab[i].Id, Tab[i].File, Dst, Check);
        if (Err != FAT_OK) {
            return Err;
        }
        if (Kind == STORE_KIND_FONT) {
            (void)FontReloadAssets();
            ThemeClampFontId();
        }
        (void)StoreMarkInstalled(Tab[i].Id, Tab[i].Type, Tab[i].File);
        return FAT_OK;
    }
    return FAT_ERR_NOENT;
}

static int MakeDbKey(char *Out, int Max, const char *Prefix, const char *Id) {
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

static int StoreMarkInstalled(const char *Id, const char *Type, const char *File) {
    char Key[DB_KEY_MAX];
    char DepKey[DB_KEY_MAX];
    char Val[DB_VAL_MAX];
    int i = 0;
    int j;

    if (!MakeDbKey(Key, (int)sizeof(Key), "si.", Id)) {
        return FAT_ERR_INVAL;
    }
    /* type|file */
    for (j = 0; Type && Type[j] && i < (int)sizeof(Val) - 1; j++) {
        Val[i++] = Type[j];
    }
    if (i < (int)sizeof(Val) - 1) {
        Val[i++] = '|';
    }
    for (j = 0; File && File[j] && i < (int)sizeof(Val) - 1; j++) {
        Val[i++] = File[j];
    }
    Val[i] = 0;
    if (DbSet(Key, Val) != DB_OK) {
        return FAT_ERR_IO;
    }
    /* 依赖占位（M1 再写真实 depends） */
    if (MakeDbKey(DepKey, (int)sizeof(DepKey), "sd.", Id)) {
        (void)DbSet(DepKey, "-");
    }
    return FAT_OK;
}

typedef struct {
    STORE_INSTALLED *Out;
    int Max;
    int Count;
} STORE_LIST_CTX;

static int ListInstalledCb(const char *Key, const char *Value, void *Ctx) {
    STORE_LIST_CTX *C = (STORE_LIST_CTX *)Ctx;
    STORE_INSTALLED *E;
    const char *Bar;
    int i;

    if (!Key || Key[0] != 's' || Key[1] != 'i' || Key[2] != '.') {
        return 0;
    }
    if (!C || !C->Out || C->Count >= C->Max) {
        return 1;
    }
    E = &C->Out[C->Count];
    for (i = 0; Key[3 + i] && i < STORE_ID_MAX - 1; i++) {
        E->Id[i] = Key[3 + i];
    }
    E->Id[i] = 0;
    E->Type[0] = 0;
    E->File[0] = 0;
    if (!Value) {
        C->Count++;
        return 0;
    }
    Bar = Value;
    while (*Bar && *Bar != '|') {
        Bar++;
    }
    {
        int n = 0;
        while (Value + n < Bar && n < (int)sizeof(E->Type) - 1) {
            E->Type[n] = Value[n];
            n++;
        }
        E->Type[n] = 0;
    }
    if (*Bar == '|') {
        Bar++;
        for (i = 0; Bar[i] && i < STORE_FILE_MAX - 1; i++) {
            E->File[i] = Bar[i];
        }
        E->File[i] = 0;
    }
    C->Count++;
    return 0;
}

int StoreListInstalled(STORE_INSTALLED *Out, int Max, int *OutCount) {
    STORE_LIST_CTX Ctx;

    if (!Out || Max <= 0 || !OutCount) {
        return FAT_ERR_INVAL;
    }
    Ctx.Out = Out;
    Ctx.Max = Max;
    Ctx.Count = 0;
    (void)DbForEach(ListInstalledCb, &Ctx);
    *OutCount = Ctx.Count;
    return FAT_OK;
}

int StoreRemove(const char *Id) {
    char Key[DB_KEY_MAX];
    char DepKey[DB_KEY_MAX];
    char Val[DB_VAL_MAX];
    char Type[12];
    char File[STORE_FILE_MAX];
    char Dst[96];
    const char *Bar;
    int i;
    int Kind;
    int Err;

    if (!Id || Id[0] == 0) {
        return FAT_ERR_INVAL;
    }
    if (!MakeDbKey(Key, (int)sizeof(Key), "si.", Id)) {
        return FAT_ERR_INVAL;
    }
    if (DbGet(Key, Val, sizeof(Val)) != DB_OK) {
        return FAT_ERR_NOENT;
    }
    Bar = Val;
    while (*Bar && *Bar != '|') {
        Bar++;
    }
    i = 0;
    while (Val + i < Bar && i < (int)sizeof(Type) - 1) {
        Type[i] = Val[i];
        i++;
    }
    Type[i] = 0;
    File[0] = 0;
    if (*Bar == '|') {
        Bar++;
        for (i = 0; Bar[i] && i < STORE_FILE_MAX - 1; i++) {
            File[i] = Bar[i];
        }
        File[i] = 0;
    }
    if (File[0] == 0) {
        (void)DbDelete(Key);
        return FAT_ERR_INVAL;
    }

    Kind = EntryKind(Type);
    if (Kind == STORE_KIND_FONT) {
        JoinPath(Dst, (int)sizeof(Dst), STORE_FONTS_DIR, File);
    } else if (Kind == STORE_KIND_ASSET) {
        JoinPath(Dst, (int)sizeof(Dst), STORE_PACKS_DIR, File);
    } else {
        JoinPath(Dst, (int)sizeof(Dst), STORE_APPS_DIR, File);
    }

    Err = FileSystemDeleteFile(Dst);
    /* vvfat：删刚写入文件曾宿主断言；现 FatDeleteFile 已改为只摘目录项 */
    if (Err != FAT_OK && Err != FAT_ERR_NOENT) {
        return Err;
    }
    (void)DbDelete(Key);
    if (MakeDbKey(DepKey, (int)sizeof(DepKey), "sd.", Id)) {
        (void)DbDelete(DepKey);
    }
    if (Kind == STORE_KIND_FONT) {
        (void)FontReloadAssets();
        ThemeClampFontId();
    }
    return FAT_OK;
}
