/*
 * StoreInstall.c — 单包安装与登记
 * 核心：Store.c。组合包仍在 Store.c。
 */
#include "Store.h"
#include "StorePrivate.h"
#include "FileSystem.h"
#include "Fat.h"
#include "PhysicalMemory.h"
#include "HalConsole.h"
#include "Hal.h"
#include "Db.h"

#define STORE_COPY_MAX     FAT_WRITE_MAX
/* 装卸拷贝：每块后 StoreIoBreath，避免整 ELF 写盘时鼠标冻住 */
#define STORE_IO_BREATH_BYTES  4096u
#define STORE_CHECK_NONE   0
#define STORE_CHECK_ELF    1
#define STORE_CHECK_TOYF   2

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
    while (Got < Size) {
        UINTN Chunk = Size - Got;
        UINTN N = 0;

        if (Chunk > STORE_IO_BREATH_BYTES) {
            Chunk = STORE_IO_BREATH_BYTES;
        }
        StoreIoBreath();
        Err = FileSystemReadFileAt(Src, Got, Buf + Got, Chunk, &N);
        if (Err != FAT_OK || N != Chunk) {
            PhysicalMemoryFreePages(Buf, Pages);
            return Err != FAT_OK ? Err : FAT_ERR_IO;
        }
        Got += N;
    }
    StoreIoBreath();
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
    (void)StoreDeleteManagedFile(Dst);
    StoreIoBreath();
    /* 一次 WriteFile：避免分块 WriteFileAt 在 USB 上重复建同名目录项 */
    FatSetIoBreath(StoreIoBreath);
    Err = FileSystemWriteFile(Dst, Buf, Got);
    FatSetIoBreath(0);
    StoreIoBreath();
    if (Err != FAT_OK) {
        HalConsoleWriteSerial("store: write failed\n");
    }
    PhysicalMemoryFreePages(Buf, Pages);
    return Err;
}

static int InstallFromSources(const char *Id, const char *File, const char *Dst,
                              int Check) {
    char Src[128];
    char Pkg[160];
    int Err;

    /* 1) StoreCache/<file> 仓库包；2) Assets/Store/packages/<id>/；3) 卷根教学镜像 */
    JoinPath(Src, (int)sizeof(Src), STORE_CACHE_DIR, File);
    Err = TryCopy(Src, Dst, Check);
    if (Err == FAT_OK) {
        return FAT_OK;
    }
    JoinPath(Pkg, (int)sizeof(Pkg), "Assets/Store/packages", Id);
    JoinPath(Src, (int)sizeof(Src), Pkg, File);
    Err = TryCopy(Src, Dst, Check);
    if (Err == FAT_OK) {
        return FAT_OK;
    }
    return TryCopy(File, Dst, Check);
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
            HalConsoleWriteSerial("store: bad type (app|font|asset|lib)\n");
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
        HalConsoleWriteSerial("store: install copy failed\n");
        return Err;
    }
        if (Kind == STORE_KIND_FONT) {
            gNeedFontReload = 1;
            if (gStoreComboDepth == 0) {
                StoreFlushFontReload();
            }
        }
        (void)StoreMarkInstalled(Tab[i].Id, Tab[i].Type, Tab[i].File, DepBuf);
        return FAT_OK;
    }
    return FAT_ERR_NOENT;
}

/*
 * 镜像预置仅有文件、无 si.*：补登记，否则「Install 成功 → Remove 必 fail」。
 */

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

int StoreAdoptInstalled(const char *Id) {
    STORE_ENTRY *Tab = gStoreTab;
    int Count = 0;
    int i;
    int Err;
    char DepBuf[STORE_DEPENDS_MAX];

    if (StoreHasSi(Id)) {
        return FAT_OK;
    }
    Err = StoreLoadCatalog(Tab, STORE_ENTRIES_MAX, &Count);
    if (Err < 0) {
        return Err;
    }
    for (i = 0; i < Count; i++) {
        if (!StrEq(Tab[i].Id, Id)) {
            continue;
        }
        CopyStr(DepBuf, (int)sizeof(DepBuf), Tab[i].Depends);
        if (LoadPkgDepends(Tab[i].Id, DepBuf, (int)sizeof(DepBuf))) {
            /* PKG 覆盖 */
        }
        NormalizeDepends(DepBuf);
        return StoreMarkInstalled(Tab[i].Id, Tab[i].Type, Tab[i].File, DepBuf);
    }
    return FAT_ERR_NOENT;
}
