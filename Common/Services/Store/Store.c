/*
 * Store.c — PR-S1：离线 catalog 安装；PR-S3：font/asset → Assets/
 *           PR-S4：ToyDB 已装清单 + store remove
 *           PR-M1：depends=（catalog 第 8 段 / PKG.TXT）；缺依赖拒绝安装
 *           PR-M2：store combo / uncombo — 按依赖顺序装卸多包「功能」
 *
 * 载荷查找顺序：Store/<file> → Assets/Store/packages/<id>/<file> → 卷根 <file>
 *
 * 卸装删的是 Apps/（或 Fonts/Packs）里的已装副本，不是 Store/ 仓库。
 * 只删 U 盘 Store 目录下的 ELF：商店目录仍在（catalog）；已装的仍在 Apps/；
 * 卷根还有 HELLO.ELF 等教学镜像，Install 仍可能成功。
 * sha256=- 时跳过校验（教学默认）。
 * 清单键：si.<id>=type|file ；依赖 sd.<id>=逗号 id 或 -
 * catalog 解析在 StoreCatalog.c。单包安装在 StoreInstall.c。组合包在 StoreCombo.c。
 */
#include "Store.h"
#include "StorePriv.h"
#include "FileSystem.h"
#include "Fat.h"
#include "PhysicalMemory.h"
#include "HalConsole.h"
#include "Hal.h"
#include "Gui.h"
#include "Font.h"
#include "Theme.h"
#include "Db.h"

#define STORE_PKG_MAX      1024u

/* 内核任务栈仅 8KiB；catalog 表放 BSS，避免 store sync/HTTP 栈溢出闪退 */
STORE_ENTRY gStoreTab[STORE_ENTRIES_MAX];
/* combo 嵌套卸装时合并 FontReload，避免连删字体卡死/重入 */
int gStoreComboDepth;
int gNeedFontReload;
static int gStorePayloadBypass;

/*
 * 长 IO 呼吸：每块拷贝/写盘后排空 xHCI 事件环 + 让鼠标动。
 *
 * PR-S-input-drain：稳态 drain 在 YieldForPollInput（shell/gui 每轮让步处）；
 * 但长 Store 拷贝/写盘期间 GuiTask 不走 YieldForPollInput，且真机 poll-USB 下
 * MSC/FAT 完成事件需 XchiDrainEvents 推进 → 此处自带 HalInputPoll 兜底。
 * 序 1「或等价」：drain 集中在 yield 路径 + IO 呼吸两处，GuiPollMouse 等只 dequeue。
 */
void StoreIoBreath(void) {
    HalInputPoll();
    GuiPollMouseMotion();
}

void StoreFlushFontReload(void) {
    if (!gNeedFontReload) {
        return;
    }
    gNeedFontReload = 0;
    StoreIoBreath();
    (void)FontReloadAssets();
    ThemeClampFontId();
    /* 字高变了须重合成，否则桌面仍按旧度量画 */
    GuiComposeThemeScene();
    /* PR-S-compose-sep（序 4）：合成后、写盘前呼吸一次，避免 compose+ThemeSave
     * 连续长消费段冻住光标；ThemeSave 内 DB 写盘走 FatSetIoBreath 持续呼吸 */
    StoreIoBreath();
    FatSetIoBreath(StoreIoBreath);
    (void)ThemeSave();
    FatSetIoBreath(0);
    StoreIoBreath();
}

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

static int StrEqIgnoreCase(const char *A, const char *B) {
    if (!A || !B) {
        return 0;
    }
    while (*A && *B) {
        char Ca = *A;
        char Cb = *B;
        if (Ca >= 'a' && Ca <= 'z') {
            Ca = (char)(Ca - 'a' + 'A');
        }
        if (Cb >= 'a' && Cb <= 'z') {
            Cb = (char)(Cb - 'a' + 'A');
        }
        if (Ca != Cb) {
            return 0;
        }
        A++;
        B++;
    }
    return *A == 0 && *B == 0;
}

/* FAT 上文件名大小写不一（HELLO.ELF vs hello.elf） */
static int DirHasFileCI(const char *Dir, const char *File) {
    static FAT_DIRECTORY_ENTRY Ents[FAT_LIST_MAX];
    int N = 0;
    int i;

    if (!Dir || !File || File[0] == 0) {
        return 0;
    }
    if (FileSystemListEntries(Dir, Ents, FAT_LIST_MAX, &N) != FAT_OK || N <= 0) {
        return 0;
    }
    for (i = 0; i < N; i++) {
        if (Ents[i].Attr & FAT_ATTR_DIR) {
            continue;
        }
        if (StrEqIgnoreCase(Ents[i].Name, File)) {
            return 1;
        }
    }
    return 0;
}

/* 返回目录里真实名（大小写以盘为准），供 Delete 路径 */
static int DirResolveFileCI(const char *Dir, const char *File, char *Out, int OutMax) {
    static FAT_DIRECTORY_ENTRY Ents[FAT_LIST_MAX];
    char Want[STORE_FILE_MAX];
    int N = 0;
    int i;
    int w;

    if (!Out || OutMax <= 0) {
        return 0;
    }
    Out[0] = 0;
    if (!Dir || !File || File[0] == 0) {
        return 0;
    }
    /* File 与 Out 可能同缓冲：先拷走 */
    w = 0;
    while (File[w] && w + 1 < (int)sizeof(Want)) {
        Want[w] = File[w];
        w++;
    }
    Want[w] = 0;
    if (Want[0] == 0) {
        return 0;
    }
    if (FileSystemListEntries(Dir, Ents, FAT_LIST_MAX, &N) != FAT_OK || N <= 0) {
        return 0;
    }
    for (i = 0; i < N; i++) {
        if (Ents[i].Attr & FAT_ATTR_DIR) {
            continue;
        }
        if (StrEqIgnoreCase(Ents[i].Name, Want)) {
            int k = 0;
            while (Ents[i].Name[k] && k + 1 < OutMax) {
                Out[k] = Ents[i].Name[k];
                k++;
            }
            Out[k] = 0;
            return Out[0] != 0;
        }
    }
    return 0;
}


/* 规范化：空 / "-" → 空串（表示无依赖） */
void NormalizeDepends(char *Dep) {
    if (!Dep) {
        return;
    }
    if (Dep[0] == 0 || (Dep[0] == '-' && Dep[1] == 0)) {
        Dep[0] = 0;
    }
}

int ArchOk(const char *Arch) {
    const char *Host;

    if (!Arch || Arch[0] == 0 || StrEq(Arch, "any")) {
        return 1;
    }
    Host = StoreHostArch();
    return StrEq(Arch, Host);
}

static int EnsureDir(const char *Path) {
    int Err = FileSystemMakeDirectory(Path);
    if (Err == FAT_OK || Err == FAT_ERR_EXIST) {
        return FAT_OK;
    }
    return Err;
}

int EnsureAppsDir(void) {
    return EnsureDir(STORE_APPS_DIR);
}

int EnsureFontsDir(void) {
    int Err = EnsureDir("Assets");
    if (Err != FAT_OK) {
        return Err;
    }
    return EnsureDir(STORE_FONTS_DIR);
}

int EnsurePacksDir(void) {
    int Err = EnsureDir("Assets");
    if (Err != FAT_OK) {
        return Err;
    }
    return EnsureDir(STORE_PACKS_DIR);
}

int EntryKind(const char *Type) {
    if (StrEq(Type, "font")) {
        return STORE_KIND_FONT;
    }
    if (StrEq(Type, "asset")) {
        return STORE_KIND_ASSET;
    }
    if (StrEq(Type, "lib") || StrEq(Type, "library")) {
        return STORE_KIND_LIB;
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





int MakeDbKey(char *Out, int Max, const char *Prefix, const char *Id) {
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

/* si.* 或 catalog 的 type → STORE_KIND_*；未知 -1 */
int LookupPackageKind(const char *Id) {
    char Key[DB_KEY_MAX];
    char Val[DB_VAL_MAX];
    char Type[12];
    STORE_ENTRY *Tab;
    int Count = 0;
    int i;
    int Err;

    if (!Id || Id[0] == 0) {
        return -1;
    }
    if (MakeDbKey(Key, (int)sizeof(Key), "si.", Id) &&
        DbGet(Key, Val, sizeof(Val)) == DB_OK) {
        i = 0;
        while (Val[i] && Val[i] != '|' && i < (int)sizeof(Type) - 1) {
            Type[i] = Val[i];
            i++;
        }
        Type[i] = 0;
        return EntryKind(Type);
    }
    Tab = gStoreTab;
    Err = StoreLoadCatalog(Tab, STORE_ENTRIES_MAX, &Count);
    if (Err < 0 || Count <= 0) {
        return -1;
    }
    for (i = 0; i < Count; i++) {
        if (StrEq(Tab[i].Id, Id)) {
            return EntryKind(Tab[i].Type);
        }
    }
    return -1;
}

int StoreHasSi(const char *Id) {
    char Key[DB_KEY_MAX];
    char Val[DB_VAL_MAX];

    if (!Id || Id[0] == 0) {
        return 0;
    }
    if (!MakeDbKey(Key, (int)sizeof(Key), "si.", Id)) {
        return 0;
    }
    return DbGet(Key, Val, sizeof(Val)) == DB_OK ? 1 : 0;
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

/* 删托管载荷并刷盘；已 Resolve 存在时，勿把 NOENT 当成功。
 * 同名多目录项时循环摘除，直到 Dir 扫不到。 */
static int StoreUnlinkPayload(const char *Dir, const char *Want) {
    char Dst[96];
    char Prefixed[112];
    char Leaf[STORE_FILE_MAX];
    FAT_FILE_STAT St;
    int Err;
    int Round;

    if (!Dir || !Want || Want[0] == 0) {
        return FAT_ERR_INVAL;
    }

    for (Round = 0; Round < 8; Round++) {
        if (!DirResolveFileCI(Dir, Want, Leaf, (int)sizeof(Leaf))) {
            return FAT_OK;
        }
        JoinPath(Dst, (int)sizeof(Dst), Dir, Leaf);
        StoreIoBreath();
        Err = StoreDeleteManagedFile(Dst);
        (void)FileSystemFileSync("");
        if (Err == FAT_ERR_NOENT) {
            int n = 0;
            const char *Pre = "TOYOS:";
            while (Pre[n]) {
                Prefixed[n] = Pre[n];
                n++;
            }
            CopyStr(Prefixed + n, (int)sizeof(Prefixed) - n, Dst);
            Err = StoreDeleteManagedFile(Prefixed);
            (void)FileSystemFileSync("");
        }
        if (Err != FAT_OK && Err != FAT_ERR_NOENT) {
            return Err;
        }
        if (!DirHasFileCI(Dir, Want) && FileSystemFileStat(Dst, &St) != FAT_OK) {
            return FAT_OK;
        }
    }
    JoinPath(Dst, (int)sizeof(Dst), Dir, Want);
    if (DirHasFileCI(Dir, Want) || FileSystemFileStat(Dst, &St) == FAT_OK) {
        HalConsoleWriteSerial("store: remove failed (file remains) ");
        HalConsoleWriteSerial(Dst);
        HalConsoleWriteSerial("\n");
        return FAT_ERR_IO;
    }
    return FAT_OK;
}

int StoreRemove(const char *Id) {
    char Key[DB_KEY_MAX];
    char DepKey[DB_KEY_MAX];
    char Val[DB_VAL_MAX];
    char Type[12];
    char File[STORE_FILE_MAX];
    char Real[STORE_FILE_MAX];
    char Users[STORE_INSTALLED_MAX][STORE_ID_MAX];
    STORE_ENTRY *Tab;
    const char *Bar;
    const char *Dir;
    int i;
    int Kind;
    int Err;
    int UserN;
    int Count = 0;

    if (!Id || Id[0] == 0) {
        return FAT_ERR_INVAL;
    }
    if (!MakeDbKey(Key, (int)sizeof(Key), "si.", Id)) {
        return FAT_ERR_INVAL;
    }
    if (DbGet(Key, Val, sizeof(Val)) != DB_OK) {
        /*
         * 无 si.* 但仍标 installed（仅盘上有文件）：按 catalog 卸文件。
         * 未先 Install/Adopt 时直接 Remove 也能成。
         */
        Tab = gStoreTab;
        Err = StoreLoadCatalog(Tab, STORE_ENTRIES_MAX, &Count);
        if (Err < 0 || Count <= 0) {
            return FAT_ERR_NOENT;
        }
        for (i = 0; i < Count; i++) {
            if (!StrEq(Tab[i].Id, Id)) {
                continue;
            }
            Kind = EntryKind(Tab[i].Type);
            if (Kind == STORE_KIND_FONT) {
                Dir = STORE_FONTS_DIR;
            } else if (Kind == STORE_KIND_ASSET || Kind == STORE_KIND_LIB) {
                Dir = STORE_PACKS_DIR;
            } else if (Kind == STORE_KIND_APP) {
                Dir = STORE_APPS_DIR;
            } else {
                return FAT_ERR_INVAL;
            }
            if (!DirResolveFileCI(Dir, Tab[i].File, Real, (int)sizeof(Real))) {
                return FAT_ERR_NOENT;
            }
            UserN = CollectDependents(Id, Users, STORE_INSTALLED_MAX);
            if (UserN > 0) {
                HalConsoleWriteSerial("store: still required by dependents\n");
                return FAT_ERR_INVAL;
            }
            Err = StoreUnlinkPayload(Dir, Real);
            StoreIoBreath();
            if (Err != FAT_OK) {
                return Err;
            }
            if (Kind == STORE_KIND_FONT) {
                gNeedFontReload = 1;
                if (gStoreComboDepth == 0) {
                    StoreFlushFontReload();
                }
            }
            return FAT_OK;
        }
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
        Dir = STORE_FONTS_DIR;
    } else if (Kind == STORE_KIND_ASSET || Kind == STORE_KIND_LIB) {
        Dir = STORE_PACKS_DIR;
    } else {
        Dir = STORE_APPS_DIR;
    }
    if (!DirResolveFileCI(Dir, File, Real, (int)sizeof(Real))) {
        /* si 指向已无文件：仍清 DB，算卸装成功 */
        (void)DbDelete(Key);
        if (MakeDbKey(DepKey, (int)sizeof(DepKey), "sd.", Id)) {
            (void)DbDelete(DepKey);
        }
        return FAT_OK;
    }

    Err = StoreUnlinkPayload(Dir, Real);
    StoreIoBreath();
    if (Err != FAT_OK) {
        return Err;
    }
    (void)DbDelete(Key);
    if (MakeDbKey(DepKey, (int)sizeof(DepKey), "sd.", Id)) {
        (void)DbDelete(DepKey);
    }
    if (Kind == STORE_KIND_FONT) {
        gNeedFontReload = 1;
        if (gStoreComboDepth == 0) {
            StoreFlushFontReload();
        }
    }
    return FAT_OK;
}


int StoreIsInstalled(const char *Id) {
    char Key[DB_KEY_MAX];
    char DepKey[DB_KEY_MAX];
    char Val[DB_VAL_MAX];
    char Type[12];
    char File[STORE_FILE_MAX];
    const char *Bar;
    const char *Dir;
    STORE_ENTRY *Tab;
    int Count = 0;
    int i;
    int Kind;
    int Err;

    if (!Id || Id[0] == 0) {
        return 0;
    }
    if (!MakeDbKey(Key, (int)sizeof(Key), "si.", Id)) {
        return 0;
    }
    if (DbGet(Key, Val, sizeof(Val)) == DB_OK) {
        /* si.* 须对应盘上文件；否则清孤儿清单（Files 曾直删时） */
        Bar = Val;
        i = 0;
        while (Val[i] && Val[i] != '|' && i < (int)sizeof(Type) - 1) {
            Type[i] = Val[i];
            i++;
        }
        Type[i] = 0;
        File[0] = 0;
        while (*Bar && *Bar != '|') {
            Bar++;
        }
        if (*Bar == '|') {
            Bar++;
            for (i = 0; Bar[i] && i < STORE_FILE_MAX - 1; i++) {
                File[i] = Bar[i];
            }
            File[i] = 0;
        }
        Kind = EntryKind(Type);
        if (Kind == STORE_KIND_FONT) {
            Dir = STORE_FONTS_DIR;
        } else if (Kind == STORE_KIND_ASSET || Kind == STORE_KIND_LIB) {
            Dir = STORE_PACKS_DIR;
        } else {
            Dir = STORE_APPS_DIR;
        }
        if (File[0] && DirHasFileCI(Dir, File)) {
            return 1;
        }
        (void)DbDelete(Key);
        if (MakeDbKey(DepKey, (int)sizeof(DepKey), "sd.", Id)) {
            (void)DbDelete(DepKey);
        }
        return 0;
    }
    /*
     * 镜像预置（Apps/HELLO.ELF 等）无 si.* 时：按 catalog 落盘路径探测。
     * 开始菜单扫目录能看到，商店也应标 installed。
     */
    Tab = gStoreTab;
    Err = StoreLoadCatalog(Tab, STORE_ENTRIES_MAX, &Count);
    if (Err < 0 || Count <= 0) {
        return 0;
    }
    for (i = 0; i < Count; i++) {
        if (!StrEq(Tab[i].Id, Id)) {
            continue;
        }
        Kind = EntryKind(Tab[i].Type);
        if (Kind == STORE_KIND_APP) {
            return DirHasFileCI(STORE_APPS_DIR, Tab[i].File);
        }
        if (Kind == STORE_KIND_FONT) {
            return DirHasFileCI(STORE_FONTS_DIR, Tab[i].File);
        }
        if (Kind == STORE_KIND_ASSET || Kind == STORE_KIND_LIB) {
            return DirHasFileCI(STORE_PACKS_DIR, Tab[i].File);
        }
        return 0;
    }
    return 0;
}

void StoreFillInstalledFlags(const STORE_ENTRY *Tab, int Count, int *OutFlags) {
    static FAT_DIRECTORY_ENTRY Apps[FAT_LIST_MAX];
    static FAT_DIRECTORY_ENTRY Fonts[FAT_LIST_MAX];
    static FAT_DIRECTORY_ENTRY Packs[FAT_LIST_MAX];
    char Key[DB_KEY_MAX];
    char Val[DB_VAL_MAX];
    int Na = -1;
    int Nf = -1;
    int Np = -1;
    int i;
    int j;
    int Kind;

    if (!OutFlags) {
        return;
    }
    if (!Tab || Count <= 0) {
        return;
    }
    if (Count > STORE_ENTRIES_MAX) {
        Count = STORE_ENTRIES_MAX;
    }

    for (i = 0; i < Count; i++) {
        OutFlags[i] = 0;
        if (!Tab[i].Id[0]) {
            continue;
        }
        if (MakeDbKey(Key, (int)sizeof(Key), "si.", Tab[i].Id) &&
            DbGet(Key, Val, sizeof(Val)) == DB_OK) {
            /* 与 StoreIsInstalled 一致：有 si 无文件 → 不算已装（清孤儿） */
            OutFlags[i] = StoreIsInstalled(Tab[i].Id) ? 1 : 0;
            continue;
        }
        Kind = EntryKind(Tab[i].Type);
        if (Kind == STORE_KIND_APP) {
            if (Na < 0) {
                Na = 0;
                if (FileSystemListEntries(STORE_APPS_DIR, Apps, FAT_LIST_MAX, &Na) !=
                    FAT_OK) {
                    Na = 0;
                }
                StoreIoBreath();
            }
            for (j = 0; j < Na; j++) {
                if (!(Apps[j].Attr & FAT_ATTR_DIR) &&
                    StrEqIgnoreCase(Apps[j].Name, Tab[i].File)) {
                    OutFlags[i] = 1;
                    break;
                }
            }
        } else if (Kind == STORE_KIND_FONT) {
            if (Nf < 0) {
                Nf = 0;
                if (FileSystemListEntries(STORE_FONTS_DIR, Fonts, FAT_LIST_MAX, &Nf) !=
                    FAT_OK) {
                    Nf = 0;
                }
                StoreIoBreath();
            }
            for (j = 0; j < Nf; j++) {
                if (!(Fonts[j].Attr & FAT_ATTR_DIR) &&
                    StrEqIgnoreCase(Fonts[j].Name, Tab[i].File)) {
                    OutFlags[i] = 1;
                    break;
                }
            }
        } else if (Kind == STORE_KIND_ASSET) {
            if (Np < 0) {
                Np = 0;
                if (FileSystemListEntries(STORE_PACKS_DIR, Packs, FAT_LIST_MAX, &Np) !=
                    FAT_OK) {
                    Np = 0;
                }
                StoreIoBreath();
            }
            for (j = 0; j < Np; j++) {
                if (!(Packs[j].Attr & FAT_ATTR_DIR) &&
                    StrEqIgnoreCase(Packs[j].Name, Tab[i].File)) {
                    OutFlags[i] = 1;
                    break;
                }
            }
        }
    }
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

int StorePayloadBypassActive(void) {
    return gStorePayloadBypass > 0;
}

int StoreDeleteManagedFile(const char *Path) {
    int Err;

    gStorePayloadBypass++;
    Err = FileSystemDeleteFile(Path);
    if (gStorePayloadBypass > 0) {
        gStorePayloadBypass--;
    }
    return Err;
}

int StoreUnregister(const char *Id) {
    char Key[DB_KEY_MAX];
    char DepKey[DB_KEY_MAX];

    if (!Id || Id[0] == 0) {
        return FAT_ERR_INVAL;
    }
    if (!MakeDbKey(Key, (int)sizeof(Key), "si.", Id)) {
        return FAT_ERR_INVAL;
    }
    (void)DbDelete(Key);
    if (MakeDbKey(DepKey, (int)sizeof(DepKey), "sd.", Id)) {
        (void)DbDelete(DepKey);
    }
    return FAT_OK;
}

static int PathEndsWithElf(const char *Name) {
    int N = 0;

    if (!Name) {
        return 0;
    }
    while (Name[N]) {
        N++;
    }
    if (N < 4) {
        return 0;
    }
    return StrEqIgnoreCase(Name + N - 4, ".elf");
}

static int RelUnderDir(const char *Rel, const char *Dir, const char **OutLeaf) {
    int i = 0;

    if (!Rel || !Dir || !OutLeaf) {
        return 0;
    }
    while (Rel[0] == '/' || Rel[0] == '\\') {
        Rel++;
    }
    while (Dir[i]) {
        char Ca = Dir[i];
        char Cb = Rel[i];
        if (Ca >= 'a' && Ca <= 'z') {
            Ca = (char)(Ca - 'a' + 'A');
        }
        if (Cb >= 'a' && Cb <= 'z') {
            Cb = (char)(Cb - 'a' + 'A');
        }
        if (Ca != Cb) {
            return 0;
        }
        i++;
    }
    if (Rel[i] != '/' && Rel[i] != '\\') {
        return 0;
    }
    *OutLeaf = Rel + i + 1;
    return (*OutLeaf)[0] != 0;
}

static int SiListsFile(const char *File) {
    STORE_INSTALLED Inst[STORE_INSTALLED_MAX];
    int N = 0;
    int i;

    if (!File || !File[0]) {
        return 0;
    }
    if (StoreListInstalled(Inst, STORE_INSTALLED_MAX, &N) != FAT_OK) {
        return 0;
    }
    for (i = 0; i < N; i++) {
        if (StrEqIgnoreCase(Inst[i].File, File)) {
            return 1;
        }
    }
    return 0;
}

static int CatalogListsPayload(const char *File, int WantKind) {
    STORE_ENTRY *Tab = gStoreTab;
    int Count = 0;
    int i;

    if (!File || !File[0]) {
        return 0;
    }
    if (StoreLoadCatalog(Tab, STORE_ENTRIES_MAX, &Count) < 0 || Count <= 0) {
        return 0;
    }
    for (i = 0; i < Count; i++) {
        if (EntryKind(Tab[i].Type) == WantKind &&
            StrEqIgnoreCase(Tab[i].File, File)) {
            return 1;
        }
    }
    return 0;
}

int StoreIsManagedPayload(const char *Path) {
    const char *Rel = Path;
    const char *Leaf = 0;
    int Vol;

    if (!Path || Path[0] == 0) {
        return 0;
    }
    if (FileSystemResolve(Path, &Vol, &Rel) != FAT_OK) {
        Rel = Path;
    }
    (void)Vol;
    if (RelUnderDir(Rel, STORE_APPS_DIR, &Leaf)) {
        if (!PathEndsWithElf(Leaf)) {
            return 0; /* Apps 下 README 等可删 */
        }
        return SiListsFile(Leaf) || CatalogListsPayload(Leaf, STORE_KIND_APP);
    }
    if (RelUnderDir(Rel, STORE_FONTS_DIR, &Leaf)) {
        return SiListsFile(Leaf) || CatalogListsPayload(Leaf, STORE_KIND_FONT);
    }
    if (RelUnderDir(Rel, STORE_PACKS_DIR, &Leaf)) {
        return SiListsFile(Leaf) || CatalogListsPayload(Leaf, STORE_KIND_ASSET);
    }
    return 0;
}
