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
        BufCh(&Disk, ' ');
        BufStr(&Disk, LocStr(MSG_DEV_READY));
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

/* ---- About 页绘制（PR-DEV-ui-summary-paint） ---- */

static void SumDrawRow(UINT32 LblX, UINT32 ValX, UINT32 *Ty, UINT32 MaxY,
                       UINT32 LineH, const char *Lbl, const char *Val) {
    if (*Ty + LineH > MaxY) {
        return;
    }
    if (Lbl) {
        HalVideoDrawStringAt(LblX, *Ty, Lbl, ThemeTextMuted());
    }
    if (Val) {
        HalVideoDrawStringAt(ValX, *Ty, Val, ThemeText());
    }
    *Ty += LineH + 2;
}

static void SumComposeOs(char *Out, UINTN Max, const DEVICES_UI_SUMMARY *S) {
    BUF B;

    BufInit(&B, Out, Max);
    BufStr(&B, S->OsVersion ? S->OsVersion : "-");
    if (S->Arch && S->Arch[0]) {
        BufCh(&B, ' ');
        BufCh(&B, '(');
        BufStr(&B, S->Arch);
        BufCh(&B, ')');
    }
    BufFin(&B);
}
static void SumComposeCpu(char *Out, UINTN Max, const DEVICES_UI_SUMMARY *S) {
    BUF B;
    BufInit(&B, Out, Max);
    BufStr(&B, S->CpuInfo ? S->CpuInfo : "-");
    BufStr(&B, " x");
    BufDec64(&B, (UINT64)(S->CpuCount > 0 ? S->CpuCount : 1));
    BufFin(&B);
}

static void BufMemNum(BUF *B, UINT64 MiB, int AsGib) {
    if (!AsGib) {
        BufDec64(B, MiB);
        return;
    }
    BufDec64(B, MiB / 1024u);
    BufCh(B, '.');
    BufCh(B, (char)('0' + (MiB % 1024u) * 10u / 1024u));
}

static void SumComposeMem(char *Out, UINTN Max, const DEVICES_UI_SUMMARY *S) {
    BUF B;
    int AsGib = S->MemTotalMiB > 1024u;
    BufInit(&B, Out, Max);
    BufMemNum(&B, S->MemFreeMiB, AsGib);
    BufCh(&B, '/');
    BufMemNum(&B, S->MemTotalMiB, AsGib);
    BufCh(&B, ' ');
    BufStr(&B, AsGib ? "GiB" : "MiB");
    BufFin(&B);
}

static void SumComposeHyper(char *Out, UINTN Max, const DEVICES_UI_SUMMARY *S) {
    BUF B;
    int Zh = (LocaleGet() == LOC_LANG_ZH);

    BufInit(&B, Out, Max);
    BufStr(&B, S->Hypervisor ? (Zh ? "是" : "Yes") : (Zh ? "否" : "No"));
    BufFin(&B);
}

static void SumComposePci(char *Out, UINTN Max, const DEVICES_UI_SUMMARY *S) {
    BUF B;

    BufInit(&B, Out, Max);
    BufDec64(&B, (UINT64)S->PciCount);
    BufFin(&B);
}

static UINT32 SumMax(UINT32 A, UINT32 B) {
    return A > B ? A : B;
}

void DevicesUiPaintSummary(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    DEVICES_UI_SUMMARY S;
    UINT32 Ty;
    UINT32 MaxY;
    UINT32 LblX;
    UINT32 ValX;
    UINT32 LineH;
    UINT32 MaxLblW;
    char Val[96];

    if (W < 40 || H < 60) {
        return;
    }
    DevicesUiFillSummary(&S);

    HalVideoFillRect(X, Y, W, H, ThemePanelDetailBackground());
    if (W > 3) {
        HalVideoFillRect(X, Y, 3, H, ThemePanelSeparator());
    }

    LineH = FontAdvanceY();
    if (LineH < 16) {
        LineH = 16;
    }
    LblX = X + 14;
    /* 测量最长标签，值列起点跟着走，避免标签/值重叠 */
    MaxLblW = 0;
    MaxLblW = SumMax(MaxLblW, FontStringWidth(LocStr(MSG_DEV_OS)));
    MaxLblW = SumMax(MaxLblW, FontStringWidth(LocStr(MSG_DEV_CPU)));
    MaxLblW = SumMax(MaxLblW, FontStringWidth(LocStr(MSG_DEV_MEMORY)));
    MaxLblW = SumMax(MaxLblW, FontStringWidth(LocStr(MSG_DEV_DISKS)));
    MaxLblW = SumMax(MaxLblW, FontStringWidth(LocStr(MSG_DEV_DISPLAY)));
    MaxLblW = SumMax(MaxLblW, FontStringWidth(LocStr(MSG_DEV_HYPERVISOR)));
    MaxLblW = SumMax(MaxLblW, FontStringWidth(LocStr(MSG_DEV_PCI_COUNT)));
    ValX = LblX + MaxLblW + 12;
    if (ValX > X + W - 60) {
        ValX = X + W - 60;
    }
    Ty = Y + 12;
    MaxY = Y + H - 4;

    /* 标题 */
    if (Ty + LineH <= MaxY) {
        HalVideoDrawStringAt(LblX, Ty, LocStr(MSG_DEV_ABOUT), ThemeTextAccent());
        Ty += LineH + 6;
    }

    SumComposeOs(Val, sizeof(Val), &S);
    SumDrawRow(LblX, ValX, &Ty, MaxY, LineH, LocStr(MSG_DEV_OS), Val);
    SumComposeCpu(Val, sizeof(Val), &S);
    SumDrawRow(LblX, ValX, &Ty, MaxY, LineH, LocStr(MSG_DEV_CPU), Val);
    SumComposeMem(Val, sizeof(Val), &S);
    SumDrawRow(LblX, ValX, &Ty, MaxY, LineH, LocStr(MSG_DEV_MEMORY), Val);
    SumDrawRow(LblX, ValX, &Ty, MaxY, LineH, LocStr(MSG_DEV_DISKS), S.Disk);
    SumDrawRow(LblX, ValX, &Ty, MaxY, LineH, LocStr(MSG_DEV_DISPLAY), S.Display);
    SumComposeHyper(Val, sizeof(Val), &S);
    SumDrawRow(LblX, ValX, &Ty, MaxY, LineH, LocStr(MSG_DEV_HYPERVISOR), Val);
    SumComposePci(Val, sizeof(Val), &S);
    SumDrawRow(LblX, ValX, &Ty, MaxY, LineH, LocStr(MSG_DEV_PCI_COUNT), Val);
}
