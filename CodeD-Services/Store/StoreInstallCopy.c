/*
 * StoreInstallCopy.c — 可切片拷贝（PR-S-job-chunk）
 * 每 STORE_IO_BREATH_BYTES 可读完后返回，供 StoreJobStep 让出 Gui。
 * 写盘仍一次 WriteFile（USB 分块建项易坏）+ FatSetIoBreath。
 */
#include "Store.h"
#include "StorePrivate.h"
#include "FileSystem.h"
#include "Fat.h"
#include "PhysicalMemory.h"
#include "HalConsole.h"

#define STORE_COPY_MAX         FAT_WRITE_MAX
#define STORE_IO_BREATH_BYTES  4096u

#define STORE_CHECK_NONE  0
#define STORE_CHECK_ELF   1
#define STORE_CHECK_TOYF  2

#define COPY_IDLE   0
#define COPY_READ   1
#define COPY_WRITE  2

typedef struct {
    int Active;
    int Phase;
    int Check;
    char Src[128];
    char Dst[96];
    UINT8 *Buf;
    UINT32 Pages;
    UINTN Size;
    UINTN Got;
} STORE_COPY_CTX;

static STORE_COPY_CTX sCopy;

void StoreInstallCopyAbort(void) {
    if (sCopy.Buf && sCopy.Pages) {
        PhysicalMemoryFreePages(sCopy.Buf, sCopy.Pages);
    }
    if (sCopy.Active && sCopy.Dst[0] && sCopy.Phase == COPY_WRITE) {
        (void)StoreDeleteManagedFile(sCopy.Dst);
    }
    sCopy.Buf = 0;
    sCopy.Pages = 0;
    sCopy.Active = 0;
    sCopy.Phase = COPY_IDLE;
    sCopy.Src[0] = 0;
    sCopy.Dst[0] = 0;
    sCopy.Got = 0;
    sCopy.Size = 0;
}

int StoreInstallCopyBusy(void) {
    return sCopy.Active ? 1 : 0;
}

/* 成功 0；失败 <0。开始后须泵 StoreInstallCopyStep 至结束或 Abort。 */
int StoreInstallCopyBegin(const char *Src, const char *Dst, int Check) {
    FAT_FILE_STAT St;
    UINT32 Pages;

    StoreInstallCopyAbort();
    if (!Src || !Src[0] || !Dst || !Dst[0]) {
        return FAT_ERR_INVAL;
    }
    if (FileSystemFileStat(Src, &St) != FAT_OK || (St.Attr & FAT_ATTR_DIR)) {
        return FAT_ERR_NOENT;
    }
    if (St.Size > STORE_COPY_MAX) {
        return FAT_ERR_FILE_TOO_BIG;
    }
    /* ELF/TOYF 至少魔数 4 字节；其它（PKG/Assets）允许更小 */
    if (Check != STORE_CHECK_NONE && St.Size < 4) {
        return FAT_ERR_FILE_TOO_BIG;
    }
    if (St.Size == 0) {
        return FAT_ERR_NOENT;
    }
    Pages = (UINT32)((St.Size + 4095u) / 4096u);
    if (Pages == 0) {
        Pages = 1;
    }
    sCopy.Buf = (UINT8 *)PhysicalMemoryAllocatePages(Pages);
    if (!sCopy.Buf) {
        return FAT_ERR_NOSPC;
    }
    CopyStr(sCopy.Src, (int)sizeof(sCopy.Src), Src);
    CopyStr(sCopy.Dst, (int)sizeof(sCopy.Dst), Dst);
    sCopy.Check = Check;
    sCopy.Pages = Pages;
    sCopy.Size = St.Size;
    sCopy.Got = 0;
    sCopy.Phase = COPY_READ;
    sCopy.Active = 1;
    return FAT_OK;
}

/*
 * 1 = 还有；0 = 成功结束；<0 = 失败（已 Abort）。
 * 读相：每呼至少读一块；写相：整文件一次写出。
 */
int StoreInstallCopyStep(void) {
    UINTN Chunk;
    UINTN N;
    int Err;

    if (!sCopy.Active) {
        return FAT_ERR_INVAL;
    }

    if (sCopy.Phase == COPY_READ) {
        if (sCopy.Got >= sCopy.Size) {
            sCopy.Phase = COPY_WRITE;
            return 1;
        }
        Chunk = sCopy.Size - sCopy.Got;
        if (Chunk > STORE_IO_BREATH_BYTES) {
            Chunk = STORE_IO_BREATH_BYTES;
        }
        StoreIoBreath();
        N = 0;
        Err = FileSystemReadFileAt(sCopy.Src, sCopy.Got, sCopy.Buf + sCopy.Got, Chunk, &N);
        if (Err != FAT_OK || N != Chunk) {
            StoreInstallCopyAbort();
            return Err != FAT_OK ? Err : FAT_ERR_IO;
        }
        sCopy.Got += N;
        if (sCopy.Got < sCopy.Size) {
            return 1;
        }
        sCopy.Phase = COPY_WRITE;
        return 1;
    }

    /* COPY_WRITE */
    if (sCopy.Check == STORE_CHECK_ELF) {
        if (!(sCopy.Buf[0] == 0x7F && sCopy.Buf[1] == 'E' && sCopy.Buf[2] == 'L' &&
              sCopy.Buf[3] == 'F')) {
            StoreInstallCopyAbort();
            return FAT_ERR_INVAL;
        }
    } else if (sCopy.Check == STORE_CHECK_TOYF) {
        if (!(sCopy.Buf[0] == 'T' && sCopy.Buf[1] == 'O' && sCopy.Buf[2] == 'Y' &&
              sCopy.Buf[3] == 'F')) {
            StoreInstallCopyAbort();
            return FAT_ERR_INVAL;
        }
    }
    (void)StoreDeleteManagedFile(sCopy.Dst);
    StoreIoBreath();
    FatSetIoBreath(StoreIoBreath);
    Err = FileSystemWriteFile(sCopy.Dst, sCopy.Buf, sCopy.Got);
    FatSetIoBreath(0);
    StoreIoBreath();
    if (Err != FAT_OK) {
        HalConsoleWriteSerial("store: write failed\n");
        (void)StoreDeleteManagedFile(sCopy.Dst);
        StoreInstallCopyAbort();
        return Err;
    }
    PhysicalMemoryFreePages(sCopy.Buf, sCopy.Pages);
    sCopy.Buf = 0;
    sCopy.Pages = 0;
    sCopy.Active = 0;
    sCopy.Phase = COPY_IDLE;
    return FAT_OK;
}

/* 同步：给 Shell / 非 Job 路径 */
int StoreInstallCopySync(const char *Src, const char *Dst, int Check) {
    int Err;

    Err = StoreInstallCopyBegin(Src, Dst, Check);
    if (Err != FAT_OK) {
        return Err;
    }
    do {
        Err = StoreInstallCopyStep();
    } while (Err == 1);
    return Err;
}

void StoreInstallCopyProgress(UINTN *OutGot, UINTN *OutSize) {
    if (OutGot) {
        *OutGot = sCopy.Active ? sCopy.Got : 0;
    }
    if (OutSize) {
        *OutSize = sCopy.Active ? sCopy.Size : 0;
    }
}
