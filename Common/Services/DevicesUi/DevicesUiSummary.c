/*
 * DevicesUiSummary.c — 设备管理器「系统摘要」只读数据采集（PR-DEV-ui-summary-data）
 *
 * 第 1 刀：只建数据模型 + 填充；不改 Paint 布局。
 * 数据源均走公共 API：Hal* / PhysicalMemory* / Block* / BootInfo / Device。
 * 不 include HAL 私有头。十进制格式化自备，不拉巨型 printf。
 */
#include "DevicesUiPrivate.h"
#include "ToyOsVersion.h"
#include "Hal.h"
#include "PhysicalMemory.h"
#include "Block.h"
#include "BootInfo.h"
#include "Debug.h"

/* ---- 极简缓冲写入（自带 NUL 终止，越界截断） ---- */

typedef struct {
    char  *P;
    UINTN  N;
    UINTN  Max;
} BUF;

static void BufInit(BUF *B, char *P, UINTN Max) {
    B->P = P;
    B->N = 0;
    B->Max = Max;
    if (Max > 0) {
        P[0] = '\0';
    }
}

static void BufFin(BUF *B) {
    UINTN I = B->N < B->Max ? B->N : B->Max - 1;
    B->P[I] = '\0';
}

static void BufCh(BUF *B, char C) {
    if (B->N + 1 < B->Max) {
        B->P[(B->N)++] = C;
    }
}

static void BufStr(BUF *B, const char *S) {
    if (!S) {
        return;
    }
    while (*S) {
        BufCh(B, *S++);
    }
}

static void BufDec64(BUF *B, UINT64 V) {
    char Tmp[24];
    int  I = 0;

    if (V == 0) {
        BufCh(B, '0');
        return;
    }
    while (V > 0 && I < (int)sizeof(Tmp)) {
        Tmp[I++] = (char)('0' + (V % 10u));
        V /= 10u;
    }
    while (I > 0) {
        BufCh(B, Tmp[--I]);
    }
}

/* ---- 填充 ---- */

void DevicesUiFillSummary(DEVICES_UI_SUMMARY *Out) {
    BUF         Disk;
    BUF         Disp;
    const BOOT_INFO *Bi;
    UINT64      Pages;
    int         Ready;
    UINT32      D;

    if (!Out) {
        return;
    }

    Out->OsVersion  = TOY_OS_VERSION_STRING;
    Out->Arch      = HalArchName();
    Out->CpuInfo   = HalCpuInfo();
    Out->CpuCount  = HalCpuCount();
    Out->Hypervisor = HalCpuIsHypervisor();
    Out->PciCount  = DeviceCount();

    /* 内存：4KB 页 → MiB（1 MiB = 256 页） */
    Pages = PhysicalMemoryTotalPages();
    Out->MemTotalMiB = Pages / 256u;
    Pages = PhysicalMemoryFreePageCount();
    Out->MemFreeMiB = Pages / 256u;

    /* 硬盘：首版只报就绪盘数；不做精确容量 / SMART */
    Ready = 0;
    for (D = 0; D < BLOCK_MAX_DRIVES; D++) {
        if (BlockDriveReady(D)) {
            Ready++;
        }
    }
    BufInit(&Disk, Out->Disk, sizeof(Out->Disk));
    if (Ready > 0) {
        BufDec64(&Disk, (UINT64)Ready);
        BufStr(&Disk, " ready");
    } else {
        BufCh(&Disk, '-');
    }
    BufFin(&Disk);

    /* 显示器：BootInfo 帧缓冲宽高；无 FB 则「-」 */
    BufInit(&Disp, Out->Display, sizeof(Out->Display));
    Bi = BootInfoGet();
    if (HalHasFrameBuffer() && Bi &&
        Bi->HorizontalResolution && Bi->VerticalResolution) {
        BufDec64(&Disp, (UINT64)Bi->HorizontalResolution);
        BufCh(&Disp, 'x');
        BufDec64(&Disp, (UINT64)Bi->VerticalResolution);
    } else {
        BufCh(&Disp, '-');
    }
    BufFin(&Disp);
}

/* ---- DEBUG 自检：填结构体并串口打一行 ---- */

void DevicesUiSummarySelfCheck(void) {
    DEVICES_UI_SUMMARY S;
    BUF  L;
    char Line[192];

    DevicesUiFillSummary(&S);
    BufInit(&L, Line, sizeof(Line));
    BufStr(&L, "summary: os=");
    BufStr(&L, S.OsVersion ? S.OsVersion : "-");
    BufStr(&L, " arch=");
    BufStr(&L, S.Arch ? S.Arch : "-");
    BufStr(&L, " cpu=");
    BufStr(&L, S.CpuInfo ? S.CpuInfo : "-");
    BufStr(&L, " x");
    BufDec64(&L, (UINT64)S.CpuCount);
    BufStr(&L, " mem=");
    BufDec64(&L, S.MemFreeMiB);
    BufCh(&L, '/');
    BufDec64(&L, S.MemTotalMiB);
    BufStr(&L, "MiB disk=");
    BufStr(&L, S.Disk);
    BufStr(&L, " disp=");
    BufStr(&L, S.Display);
    BufStr(&L, " pci=");
    BufDec64(&L, (UINT64)S.PciCount);
    BufCh(&L, '\n');
    BufFin(&L);
    DebugWrite(Line);
}
