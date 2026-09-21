/*
 * ConsoleSbBar.c — Shell 客户区滚动条（PR-GUI-shell-sb）
 * 核心：ConsoleScroll.c；点选/绘制仿 FilesUi。
 */
#include "Console.h"
#include "ConsolePrivate.h"
#include "Hal.h"
#include "HalVideo.h"
#include "Gui.h"
#include "Font.h"
#include "UI.h"

#define CONSOLE_SB_W 12u

static int gBarVisible;
static UINT32 gBarX;
static UINT32 gBarY;
static UINT32 gBarW;
static UINT32 gBarH;
static int gBarFirst;
static int gBarVisibleRows;
static int gBarTotal;

void ConsoleSbBarReset(void) {
    gBarVisible = 0;
}

static void ConsoleSbBarGeom(UINT32 Cx, UINT32 Cy, UINT32 Cw, UINT32 Ch,
                             UINT32 LineH, int VisRows, int First, int Total) {
    gBarW = CONSOLE_SB_W;
    gBarH = Ch;
    gBarY = Cy;
    gBarX = (Cw > CONSOLE_SB_W + 4u) ? (Cx + Cw - CONSOLE_SB_W - 2u) : (Cx + 2u);
    gBarFirst = First;
    gBarVisibleRows = VisRows;
    gBarTotal = Total;
    gBarVisible = (Total > VisRows && VisRows > 0 && gBarH > 0) ? 1 : 0;
    (void)LineH;
}

static void ConsoleSbBarApplyTextClip(UINT32 Cx, UINT32 Cy, UINT32 Cw, UINT32 Ch,
                                      UINT32 Bg) {
    UINT32 Tw = Cw;

    if (gBarVisible && Cw > CONSOLE_SB_W + 4u) {
        Tw = Cw - CONSOLE_SB_W - 4u;
    }
    /* 必须 SetClipRegion：Origin 会把光标打回左上 → help 全挤第一行 */
    HalVideoSetClipRegion(Cx, Cy, Tw, Ch, Bg);
}

/* DrawString 里 GuiFocusApplyClip 会恢复全宽；有条时再收窄 */
void ConsoleSbBarReapplyClip(void) {
    UINT32 Cx;
    UINT32 Cy;
    UINT32 Cw;
    UINT32 Ch;
    UINT32 Bg;

    if (!gBarVisible) {
        return;
    }
    if (!GuiFocusClient(&Cx, &Cy, &Cw, &Ch, &Bg) || Cw == 0 || Ch == 0) {
        return;
    }
    ConsoleSbBarApplyTextClip(Cx, Cy, Cw, Ch, Bg);
}

int ConsoleSbBarIsVisible(void) {
    return gBarVisible;
}

static void ConsoleSbBarDraw(void) {
    if (!gBarVisible || gBarH == 0) {
        return;
    }
    HalVideoClearClip();
    UiDrawScrollBar(gBarX, gBarY, gBarW, gBarH, gBarFirst, gBarVisibleRows,
                    gBarTotal);
}

void ConsoleSbBarPrepare(UINT32 Cx, UINT32 Cy, UINT32 Cw, UINT32 Ch, UINT32 Bg,
                         UINT32 LineH, int VisRows, int First, int Total) {
    ConsoleSbBarGeom(Cx, Cy, Cw, Ch, LineH, VisRows, First, Total);
    ConsoleSbBarApplyTextClip(Cx, Cy, Cw, Ch, Bg);
}

/*
 * Repaint 末尾：绘制滚动条并保持文本 clip。
 */
void ConsoleSbBarFinishRepaint(UINT32 Cx, UINT32 Cy, UINT32 Cw, UINT32 Ch,
                               UINT32 Bg) {
    ConsoleSbBarDraw();
    ConsoleSbBarApplyTextClip(Cx, Cy, Cw, Ch, Bg);
    (void)Cw;
}

/* 实时输出：只维护条，勿整页 Paint / Backup（否则 help 叠字且极慢） */
void ConsoleSbBarAfterWrite(void) {
    UINT32 Cx;
    UINT32 Cy;
    UINT32 Cw;
    UINT32 Ch;
    UINT32 Bg;
    UINT32 LineH;
    int Vis;
    int VisRows;
    int MaxOff;
    int Start;
    int End;
    int Total;
    int Need;
    int WasVisible;

    if (!GuiShellAcceptsInput()) {
        return;
    }
    if (!GuiFocusClient(&Cx, &Cy, &Cw, &Ch, &Bg) || Ch == 0) {
        return;
    }
    LineH = FontAdvanceY();
    if (LineH < 8) {
        LineH = 16;
    }
    Vis = (int)(Ch / LineH);
    if (Vis < 1) {
        Vis = 1;
    }
    Total = ConsoleSbLineCount();
    MaxOff = ConsoleSbMaxOff(Vis);
    Need = (MaxOff > 0) ? 1 : 0;
    WasVisible = gBarVisible;

    if (!Need) {
        if (WasVisible) {
            gBarVisible = 0;
            /* 条消失：整页重排一次即可 */
            ConsoleSbPaint();
        }
        return;
    }

    End = Total - ConsoleSbViewOff();
    if (End < 0) {
        End = 0;
    }
    if (End > Total) {
        End = Total;
    }
    VisRows = Vis;
    if (ConsoleSbViewOff() == 0 && ConsoleSbAccLen() > 0) {
        VisRows = Vis - 1;
    }
    if (VisRows < 1) {
        VisRows = 1;
    }
    Start = End - VisRows;
    if (Start < 0) {
        Start = 0;
    }

    if (!WasVisible) {
        /* 首次出现：窄 clip + 重排，避免宽行与窄行叠字 */
        ConsoleSbBarGeom(Cx, Cy, Cw, Ch, LineH, VisRows, Start, Total);
        ConsoleSbPaint();
        return;
    }

    ConsoleSbBarGeom(Cx, Cy, Cw, Ch, LineH, VisRows, Start, Total);
    ConsoleSbBarDraw();
    ConsoleSbBarApplyTextClip(Cx, Cy, Cw, Ch, Bg);
}

void ConsoleOnClick(UINT32 X, UINT32 Y) {
    int NextFirst;
    int Vis;
    int MaxOff;
    int VisRows;
    int End;
    UINT32 Cx;
    UINT32 Cy;
    UINT32 Cw;
    UINT32 Ch;
    UINT32 Bg;
    UINT32 LineH;

    if (!GuiShellAcceptsInput() || !gBarVisible) {
        return;
    }
    if (!GuiFocusClient(&Cx, &Cy, &Cw, &Ch, &Bg) || Ch == 0) {
        return;
    }
    if (!UiScrollBarHit(gBarX, gBarY, gBarW, gBarH, gBarFirst, gBarVisibleRows,
                        gBarTotal, X, Y, &NextFirst)) {
        return;
    }
    LineH = FontAdvanceY();
    if (LineH < 8) {
        LineH = 16;
    }
    Vis = (int)(Ch / LineH);
    if (Vis < 1) {
        Vis = 1;
    }
    MaxOff = ConsoleSbMaxOff(Vis);
    VisRows = Vis;
    if (ConsoleSbAccLen() > 0 && NextFirst + Vis > ConsoleSbLineCount()) {
        VisRows = Vis - 1;
    }
    if (VisRows < 1) {
        VisRows = 1;
    }
    End = NextFirst + VisRows;
    if (End > ConsoleSbLineCount()) {
        End = ConsoleSbLineCount();
    }
    ConsoleSbSetViewOff(ConsoleSbLineCount() - End, MaxOff);
}
