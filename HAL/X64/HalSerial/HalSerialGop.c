/*
 * HalSerialGop.c — 开机屏上滚（PR-S-halserial-1）
 */
#include "HalSerial.h"
#include "HalSerialPrivate.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Serial.h"
#include "Font.h"
#include "ToySerialConfig.h"

static UINT32 BootLogCellW(void) {
    UINT32 Cell = FontAdvanceX();
    if (Cell == 0) {
        Cell = 8;
    }
    return Cell;
}

/* 当前分辨率下屏上最多几列（不含边距） */
static UINT32 BootLogMaxCols(void) {
    UINT32 W;
    UINT32 H;
    UINT32 Cell;
    UINT32 Cols;

    HalVideoGetSize(&W, &H);
    (void)H;
    if (W == 0) {
        W = 1024;
    }
    if (W <= BOOT_LOG_X + BOOT_LOG_MARGIN) {
        return 1;
    }
    Cell = BootLogCellW();
    Cols = (W - BOOT_LOG_X - BOOT_LOG_MARGIN) / Cell;
    if (Cols == 0) {
        Cols = 1;
    }
    if (Cols > BOOT_LOG_LINE_MAX - 1) {
        Cols = BOOT_LOG_LINE_MAX - 1;
    }
    return Cols;
}

/*
 * Ubuntu 式上滚：可见行在 RAM，满则丢最旧行并整区重绘。
 * 勿 CopyRect/memmove live front——4K 既波浪又极慢。
 */
static UINT32 BootLogVisCap(UINT32 H, UINT32 LineH) {
    UINT32 BodyY = BootLogBodyY(LineH);
    UINT32 Bottom;
    UINT32 Rows;

    if (H <= BodyY + LineH + BOOT_LOG_MARGIN) {
        return 1;
    }
    Bottom = H - BOOT_LOG_MARGIN;
    Rows = (Bottom - BodyY) / LineH;
    if (Rows == 0) {
        Rows = 1;
    }
    if (Rows > BOOT_VIS_MAX) {
        Rows = BOOT_VIS_MAX;
    }
    return Rows;
}

static void BootLogCopyRow(char *Dst, const char *Src) {
    UINTN i = 0;

    if (!Dst) {
        return;
    }
    if (!Src) {
        Dst[0] = 0;
        return;
    }
    while (Src[i] && i + 1 < BOOT_LOG_LINE_MAX) {
        Dst[i] = Src[i];
        i++;
    }
    Dst[i] = 0;
}

static void BootLogRepaintBody(UINT32 W, UINT32 H, UINT32 LineH) {
    UINT32 BodyY = BootLogBodyY(LineH);
    UINT32 Bottom;
    UINT32 i;
    UINT32 Y;

    if (W == 0) {
        W = 1024;
    }
    Bottom = (H > BOOT_LOG_MARGIN) ? (H - BOOT_LOG_MARGIN) : BodyY;
    HalVideoDrawBeginFront();
    if (Bottom > BodyY) {
        HalVideoFillRect(0, BodyY, W, Bottom - BodyY, 0x00000000u);
    }
    Y = BodyY;
    for (i = 0; i < gBootVisN; i++) {
        HalVideoDrawStringAt(BOOT_LOG_X, Y, gBootVis[i], 0x00FFFFFFu);
        Y += LineH;
    }
    HalVideoDrawEndFront();
    gBootLogY = Y;
}

static void BootLogPushVisible(UINT32 W, UINT32 H, UINT32 LineH) {
    UINT32 Cap = BootLogVisCap(H, LineH);
    UINT32 i;

    if (gBootVisN >= Cap) {
        for (i = 1; i < gBootVisN; i++) {
            BootLogCopyRow(gBootVis[i - 1], gBootVis[i]);
        }
        if (gBootVisN > 0) {
            gBootVisN--;
        }
        BootLogCopyRow(gBootVis[gBootVisN], gHalSerialLine);
        gBootVisN++;
        BootLogRepaintBody(W, H, LineH);
        return;
    }
    BootLogCopyRow(gBootVis[gBootVisN], gHalSerialLine);
    gBootVisN++;
    HalVideoDrawBeginFront();
    HalVideoDrawStringAt(BOOT_LOG_X, gBootLogY, gHalSerialLine, 0x00FFFFFFu);
    HalVideoDrawEndFront();
    gBootLogY += LineH;
}

void GopBannerOnce(void) {
#if !TOY_SCREEN_LOG
    /* SCREEN_LOG=0：不画黄字横幅，也不 Present */
    gGopBanner = 1;
    return;
#else
    {
        UINT32 W;
        UINT32 H;
        UINT32 LineH;

        if (gGopBanner) {
            return;
        }
        HalVideoGetSize(&W, &H);
        if (W == 0) {
            W = 1024;
        }
        LineH = BootLogLineH();
        HalVideoClearClip();
        HalVideoFillRect(0, 0, W, BootLogBodyY(LineH), 0x00000000u);
        HalVideoDrawStringAt(BOOT_LOG_X, BOOT_LOG_TITLE_Y, "ToyOS boot", 0x00FFFF00u);
        gBootVisN = 0;
        gBootLogY = BootLogBodyY(LineH);
        gLineLen = 0;
        gGopBanner = 1;
        /* 横幅一次性 Present 可接受（早于 xHCI）；其后镜像不再 Present */
        HalVideoPresent();
    }
#endif
}

static int gGopBatch; /* PhotoHold：凑齐再 Present，且禁止卷屏清掉枚举日志 */

static void GopFlushLine(void) {
    UINT32 W;
    UINT32 H;
    UINT32 LineH;
    UINT32 Cap;

    gHalSerialLine[gLineLen] = '\0';
    if (gLineLen == 0) {
        return;
    }
    GopBannerOnce();
    LineH = BootLogLineH();
    HalVideoGetSize(&W, &H);
    if (H == 0) {
        H = 768;
    }
    Cap = BootLogVisCap(H, LineH);
    if (gBootVisN >= Cap && gGopBatch) {
        /* 批量 Present：停笔，保留已画白字 */
        gLineLen = 0;
        return;
    }
    /*
     * boot 镜像：直写 front，禁止 Present。
     * 满行则软缓冲丢最旧并重绘；未满只追加一行。
     */
    BootLogPushVisible(W, H, LineH);
    gLineLen = 0;
}

void GopWrite(const char *Text) {
    UINT32 MaxCols;

    if (!Text || !*Text) {
        return;
    }
    GopBannerOnce();
    MaxCols = BootLogMaxCols();
    while (*Text) {
        if (*Text == '\n') {
            GopFlushLine();
            Text++;
            continue;
        }
        /* 满宽软换行：4K 用满横向，不再 160 截断 */
        if (gLineLen >= MaxCols) {
            GopFlushLine();
            MaxCols = BootLogMaxCols();
        }
        if (gLineLen + 1 < sizeof(gHalSerialLine)) {
            gHalSerialLine[gLineLen++] = *Text;
        }
        Text++;
    }
    /* 半截行留在 gHalSerialLine，等下次 '\n' —— 修复 try BAR=/0x../竖排叠字 */
}
