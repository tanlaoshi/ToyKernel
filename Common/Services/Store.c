/*
 * Store.c — PR-S1：离线 catalog 安装；PR-S3：font/asset → Assets/
 *           PR-S4：ToyDB 已装清单 + store remove
 *           PR-M1：depends=（catalog 第 8 段 / PKG.TXT）；缺依赖拒绝安装
 *           PR-M2：store combo / uncombo — 按依赖顺序装卸多包「功能」
 *
 * 载荷查找顺序：Store/<file> → <file>（卷根）→ Assets/Store/packages/<id>/<file>
 * sha256=- 时跳过校验（教学默认）。
 * 清单键：si.<id>=type|file ；依赖 sd.<id>=逗号 id 或 -
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
#define STORE_PKG_MAX      1024u
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

static void CopyStr(char *Dst, int DstMax, const char *Src) {
    int i = 0;

    if (!Dst || DstMax <= 0) {
        return;
    }
    if (!Src) {
        Dst[0] = 0;
        return;
    }
    while (Src[i] && i + 1 < DstMax) {
        Dst[i] = Src[i];
        i++;
    }
    Dst[i] = 0;
}

/* 规范化：空 / "-" → 空串（表示无依赖） */
static void NormalizeDepends(char *Dep) {
    if (!Dep) {
        return;
    }
    if (Dep[0] == 0 || (Dep[0] == '-' && Dep[1] == 0)) {
        Dep[0] = 0;
    }
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
 * PR-M1：读 packages/<id>/PKG.TXT 的 depends=；有则覆盖 catalog 段。
 * 成功写入 Out 返回 1；无文件/无键返回 0。
 */
static int LoadPkgDepends(const char *Id, char *Out, int OutMax) {
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
static int CheckDependsInstalled(const char *Depends) {
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
static int ResolveEntryDepends(const char *Id, char *OutDepends, int OutMax) {
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
static int CollectDependents(const char *Id, char OutIds[][STORE_ID_MAX], int Max) {
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

static int ComboInstallRec(const char *Id, int Depth);

int StoreComboInstall(const char *Id) {
    if (!Id || Id[0] == 0) {
        return FAT_ERR_INVAL;
    }
    return ComboInstallRec(Id, 0);
}

static int ComboInstallRec(const char *Id, int Depth) {
    char DepBuf[STORE_DEPENDS_MAX];
    char Tok[STORE_ID_MAX];
    const char *P;
    int n;
    int Err;

    if (!Id || Id[0] == 0) {
        return FAT_ERR_INVAL;
    }
    if (Depth > STORE_ENTRIES_MAX) {
        HalConsoleWriteSerial("store combo: depends cycle or too deep\n");
        return FAT_ERR_INVAL;
    }
    if (StoreIsInstalled(Id)) {
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
        Err = ComboInstallRec(Tok, Depth + 1);
        if (Err != FAT_OK) {
            return Err;
        }
    }

    HalConsoleWriteSerial("store combo: +");
    HalConsoleWriteSerial(Id);
    HalConsoleWriteSerial("\n");
    return StoreInstall(Id);
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
        return FAT_ERR_FILE_TOO_BIG;
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

static int StoreMarkInstalled(const char *Id, const char *Type, const char *File,
                              const char *Depends);

int StoreInstall(const char *Id) {
    STORE_ENTRY *Tab = gStoreTab;
    int Count = 0;
    int i;
    int Err;
    int Kind;
    int Check;
    char Dst[96];
    char DepBuf[STORE_DEPENDS_MAX];

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

        /* PR-M1：PKG.TXT depends= 覆盖 catalog 第 8 段 */
        CopyStr(DepBuf, (int)sizeof(DepBuf), Tab[i].Depends);
        if (LoadPkgDepends(Tab[i].Id, DepBuf, (int)sizeof(DepBuf))) {
            /* 已写入 DepBuf */
        }
        NormalizeDepends(DepBuf);
        Err = CheckDependsInstalled(DepBuf);
        if (Err != FAT_OK) {
            return Err;
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
        (void)StoreMarkInstalled(Tab[i].Id, Tab[i].Type, Tab[i].File, DepBuf);
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

static int StoreMarkInstalled(const char *Id, const char *Type, const char *File,
                              const char *Depends) {
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
    /* PR-M1：真实 depends；无则 "-" */
    if (MakeDbKey(DepKey, (int)sizeof(DepKey), "sd.", Id)) {
        if (Depends && Depends[0]) {
            (void)DbSet(DepKey, Depends);
        } else {
            (void)DbSet(DepKey, "-");
        }
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
    char Users[STORE_INSTALLED_MAX][STORE_ID_MAX];
    const char *Bar;
    int i;
    int Kind;
    int Err;
    int UserN;

    if (!Id || Id[0] == 0) {
        return FAT_ERR_INVAL;
    }
    if (!MakeDbKey(Key, (int)sizeof(Key), "si.", Id)) {
        return FAT_ERR_INVAL;
    }
    if (DbGet(Key, Val, sizeof(Val)) != DB_OK) {
        return FAT_ERR_NOENT;
    }

    /* PR-M2：仍有已装包依赖本 Id → 拒绝（先卸上层） */
    UserN = CollectDependents(Id, Users, STORE_INSTALLED_MAX);
    if (UserN > 0) {
        HalConsoleWriteSerial("store: still required by:");
        for (i = 0; i < UserN; i++) {
            HalConsoleWriteSerial(" ");
            HalConsoleWriteSerial(Users[i]);
        }
        HalConsoleWriteSerial("\n");
        HalConsoleWriteSerial("hint: store remove <user> first, or store uncombo <leaf>\n");
        return FAT_ERR_INVAL;
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

int StoreComboRemove(const char *Id) {
    char DepBuf[STORE_DEPENDS_MAX];
    char Tok[STORE_ID_MAX];
    char Deps[STORE_ENTRIES_MAX][STORE_ID_MAX];
    const char *P;
    int DepN = 0;
    int n;
    int i;
    int Err;
    int Users;

    if (!Id || Id[0] == 0) {
        return FAT_ERR_INVAL;
    }
    if (!StoreIsInstalled(Id)) {
        return FAT_ERR_NOENT;
    }

    DepBuf[0] = 0;
    (void)StoreGetDepends(Id, DepBuf, (int)sizeof(DepBuf));
    NormalizeDepends(DepBuf);

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

    HalConsoleWriteSerial("store uncombo: -");
    HalConsoleWriteSerial(Id);
    HalConsoleWriteSerial("\n");
    Err = StoreRemove(Id);
    if (Err != FAT_OK) {
        return Err;
    }

    /* 逆序卸依赖：仅当已无其他包引用 */
    for (i = DepN - 1; i >= 0; i--) {
        char UsersArr[STORE_INSTALLED_MAX][STORE_ID_MAX];
        if (!StoreIsInstalled(Deps[i])) {
            continue;
        }
        Users = CollectDependents(Deps[i], UsersArr, STORE_INSTALLED_MAX);
        if (Users > 0) {
            continue;
        }
        HalConsoleWriteSerial("store uncombo: -");
        HalConsoleWriteSerial(Deps[i]);
        HalConsoleWriteSerial("\n");
        Err = StoreRemove(Deps[i]);
        if (Err != FAT_OK && Err != FAT_ERR_NOENT) {
            return Err;
        }
    }
    return FAT_OK;
}

int StoreIsInstalled(const char *Id) {
    char Key[DB_KEY_MAX];
    char Val[DB_VAL_MAX];

    if (!Id || Id[0] == 0) {
        return 0;
    }
    if (!MakeDbKey(Key, (int)sizeof(Key), "si.", Id)) {
        return 0;
    }
    return DbGet(Key, Val, sizeof(Val)) == DB_OK;
}

int StoreGetDepends(const char *Id, char *Out, int OutMax) {
    char Key[DB_KEY_MAX];
    char Val[DB_VAL_MAX];
    int i;

    if (!Out || OutMax <= 0) {
        return FAT_ERR_INVAL;
    }
    Out[0] = '-';
    if (OutMax > 1) {
        Out[1] = 0;
    } else {
        Out[0] = 0;
        return FAT_ERR_INVAL;
    }
    if (!Id || Id[0] == 0) {
        return FAT_OK;
    }
    if (!MakeDbKey(Key, (int)sizeof(Key), "sd.", Id)) {
        return FAT_OK;
    }
    if (DbGet(Key, Val, sizeof(Val)) != DB_OK || Val[0] == 0) {
        return FAT_OK;
    }
    for (i = 0; Val[i] && i < OutMax - 1; i++) {
        Out[i] = Val[i];
    }
    Out[i] = 0;
    return FAT_OK;
}
