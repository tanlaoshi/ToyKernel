/*
 * Desktop.c — 桌面图标 + 双击打开 Shell / Settings（PR-D4）
 *
 * 双击：同一图标在时间窗内连点两次。首次选中（黄框），窗内再点打开；
 * 超时再点只重新选中。点空白清除选中。
 */
#include "Desktop.h"
#include "Gui.h"
#include "Console.h"
#include "UI.h"
#include "Hal.h"
#include "Font.h"
#include "Debug.h"

#define DESKTOP_ICON_COUNT   2
#define DESKTOP_ICON_SIZE    48
#define DESKTOP_ICON_GAP     28
#define DESKTOP_ORIGIN_X     36
#define DESKTOP_ORIGIN_Y     36
#define DESKTOP_LABEL_PAD    6
#define DESKTOP_DBLCLICK_SLOP 16u

/*
 * 双击时限：x86_64 用 TSC（与 LAPIC 频率无关）。
 * 约 1.0e9 周期 ≈ 0.25s@4GHz … 1s@1GHz，贴近常见双击手感。
 * 其它架构退回 HalCpuTicks，窗宽另行估计。
 */
#if defined(__x86_64__) || defined(__amd64__)
#define DESKTOP_DBLCLICK_MAX  1000000000ULL
#else
#define DESKTOP_DBLCLICK_MAX  300ULL
#endif

typedef enum {
    DESKTOP_ACT_SHELL = 0,
    DESKTOP_ACT_SETTINGS
} DESKTOP_ACTION;

typedef struct {
    const char     *Label;
    DESKTOP_ACTION  Action;
    UINT32          IconColor;
    UINT32          X;
    UINT32          Y;
} DESKTOP_ICON;

static DESKTOP_ICON gIcons[DESKTOP_ICON_COUNT];
static int gSelected = -1;
static UINT64 gSelectClock;
static UINT32 gSelectX;
static UINT32 gSelectY;

static UINT64 DesktopClock(void) {
#if defined(__x86_64__) || defined(__amd64__)
    UINT32 Lo;
    UINT32 Hi;

    __asm__ volatile("rdtsc" : "=a"(Lo), "=d"(Hi));
    return ((UINT64)Hi << 32) | Lo;
#else
    return HalCpuTicks(HalCpuId());
#endif
}

static int RectsOverlap(UINT32 Ax, UINT32 Ay, UINT32 Aw, UINT32 Ah,
                        UINT32 Bx, UINT32 By, UINT32 Bw, UINT32 Bh) {
    if (Aw == 0 || Ah == 0 || Bw == 0 || Bh == 0) {
        return 0;
    }
    return Ax < Bx + Bw && Ax + Aw > Bx && Ay < By + Bh && Ay + Ah > By;
}

static void IconBounds(const DESKTOP_ICON *Icon, UINT32 *X, UINT32 *Y,
                       UINT32 *W, UINT32 *H) {
    UINT32 LabelW;
    UINT32 TotalW;
    UINT32 TotalH;
    const char *P;

    LabelW = 0;
    if (Icon->Label) {
        for (P = Icon->Label; *P; P++) {
            LabelW += FontAdvanceX();
        }
    }
    TotalW = DESKTOP_ICON_SIZE;
    if (LabelW + 4 > TotalW) {
        TotalW = LabelW + 4;
    }
    TotalH = DESKTOP_ICON_SIZE + DESKTOP_LABEL_PAD + FontCellH();
    *X = Icon->X;
    *Y = Icon->Y;
    *W = TotalW;
    *H = TotalH;
}

static int PointInIcon(const DESKTOP_ICON *Icon, UINT32 X, UINT32 Y) {
    UINT32 Ix;
    UINT32 Iy;
    UINT32 Iw;
    UINT32 Ih;

    IconBounds(Icon, &Ix, &Iy, &Iw, &Ih);
    return X >= Ix && X < Ix + Iw && Y >= Iy && Y < Iy + Ih;
}

/* 只画不被窗口盖住的像素，避免拖动/关窗后图标盖到窗上或盖住另一图标区的窗 */
static void FillRectFree(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, UINT32 Color) {
    UINT32 Row;
    UINT32 Col;
    UINT32 RunStart;
    int InRun;

    if (W == 0 || H == 0) {
        return;
    }
    for (Row = 0; Row < H; Row++) {
        InRun = 0;
        RunStart = 0;
        for (Col = 0; Col < W; Col++) {
            int Free = !GuiPointInAnyWindow(X + Col, Y + Row);
            if (Free && !InRun) {
                RunStart = Col;
                InRun = 1;
            } else if (!Free && InRun) {
                HalVideoFillRect(X + RunStart, Y + Row, Col - RunStart, 1, Color);
                InRun = 0;
            }
        }
        if (InRun) {
            HalVideoFillRect(X + RunStart, Y + Row, W - RunStart, 1, Color);
        }
    }
}

static void DrawStringFree(UINT32 X, UINT32 Y, const char *Text, UINT32 Color) {
    UINT32 Cx = X;
    const char *P;
    char One[2];

    if (!Text) {
        return;
    }
    One[1] = 0;
    for (P = Text; *P; P++) {
        One[0] = *P;
        /* 字形左上角未被窗挡住才画，减少写穿 */
        if (!GuiPointInAnyWindow(Cx, Y)) {
            HalVideoDrawStringAt(Cx, Y, One, Color);
        }
        Cx += FontAdvanceX();
    }
}

static void DrawOneIcon(const DESKTOP_ICON *Icon, int Selected) {
    UINT32 LabelX;
    UINT32 LabelY;
    UINT32 LabelW;
    const char *P;
    UINT32 Border;

    FillRectFree(Icon->X, Icon->Y, DESKTOP_ICON_SIZE, DESKTOP_ICON_SIZE,
                 Icon->IconColor);
    Border = Selected ? COLOR_YELLOW : COLOR_WHITE;
    /* 边框用细填充近似，同样避让窗口 */
    FillRectFree(Icon->X, Icon->Y, DESKTOP_ICON_SIZE, 1, Border);
    FillRectFree(Icon->X, Icon->Y + DESKTOP_ICON_SIZE - 1, DESKTOP_ICON_SIZE, 1,
                 Border);
    FillRectFree(Icon->X, Icon->Y, 1, DESKTOP_ICON_SIZE, Border);
    FillRectFree(Icon->X + DESKTOP_ICON_SIZE - 1, Icon->Y, 1, DESKTOP_ICON_SIZE,
                 Border);
    if (Selected) {
        FillRectFree(Icon->X + 1, Icon->Y + 1, DESKTOP_ICON_SIZE - 2, 1, Border);
        FillRectFree(Icon->X + 1, Icon->Y + DESKTOP_ICON_SIZE - 2,
                     DESKTOP_ICON_SIZE - 2, 1, Border);
    }
    FillRectFree(Icon->X + DESKTOP_ICON_SIZE - 14, Icon->Y, 14, 14,
                 COLOR_LIGHT_GRAY);

    LabelW = 0;
    if (Icon->Label) {
        for (P = Icon->Label; *P; P++) {
            LabelW += FontAdvanceX();
        }
    }
    LabelX = Icon->X;
    if (LabelW < DESKTOP_ICON_SIZE) {
        LabelX = Icon->X + (DESKTOP_ICON_SIZE - LabelW) / 2;
    }
    LabelY = Icon->Y + DESKTOP_ICON_SIZE + DESKTOP_LABEL_PAD;
    if (Icon->Label) {
        DrawStringFree(LabelX, LabelY, Icon->Label,
                       Selected ? COLOR_YELLOW : COLOR_WHITE);
    }
}

static void RedrawIconIndex(int Idx) {
    if (Idx < 0 || Idx >= DESKTOP_ICON_COUNT) {
        return;
    }
    DrawOneIcon(&gIcons[Idx], Idx == gSelected);
}

static void OpenAction(DESKTOP_ACTION Action) {
    int Idx;

    if (Action == DESKTOP_ACT_SHELL) {
        Idx = GuiOpenShell();
        if (Idx >= 0) {
            ConsoleOnShellOpened();
        }
        return;
    }
    if (Action == DESKTOP_ACT_SETTINGS) {
        (void)GuiOpenSettings();
    }
}

static void SelectIcon(int Hit, UINT32 X, UINT32 Y, UINT64 Now) {
    int Prev = gSelected;

    gSelected = Hit;
    gSelectClock = Now;
    gSelectX = X;
    gSelectY = Y;
    if (Prev >= 0 && Prev != Hit) {
        RedrawIconIndex(Prev);
    }
    RedrawIconIndex(Hit);
}

void DesktopInit(void) {
    UINT32 RowH = DESKTOP_ICON_SIZE + DESKTOP_LABEL_PAD + FontCellH() +
                  DESKTOP_ICON_GAP;

    gIcons[0].Label = "Shell";
    gIcons[0].Action = DESKTOP_ACT_SHELL;
    gIcons[0].IconColor = COLOR_BLUE;
    gIcons[0].X = DESKTOP_ORIGIN_X;
    gIcons[0].Y = DESKTOP_ORIGIN_Y;

    gIcons[1].Label = "Settings";
    gIcons[1].Action = DESKTOP_ACT_SETTINGS;
    gIcons[1].IconColor = 0x00606080;
    gIcons[1].X = DESKTOP_ORIGIN_X;
    gIcons[1].Y = DESKTOP_ORIGIN_Y + RowH;

    gSelected = -1;
    gSelectClock = 0;
    gSelectX = 0;
    gSelectY = 0;
    DebugWrite("desktop: icons ready (double-click within ~0.5s)\n");
}

void DesktopDraw(void) {
    int i;

    for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
        DrawOneIcon(&gIcons[i], i == gSelected);
    }
}

void DesktopDrawRect(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    int i;
    UINT32 Ix;
    UINT32 Iy;
    UINT32 Iw;
    UINT32 Ih;

    for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
        IconBounds(&gIcons[i], &Ix, &Iy, &Iw, &Ih);
        if (RectsOverlap(X, Y, W, H, Ix, Iy, Iw, Ih)) {
            DrawOneIcon(&gIcons[i], i == gSelected);
        }
    }
}

int DesktopHandleClick(UINT32 X, UINT32 Y) {
    int i;
    int Hit;
    int Prev;
    UINT64 Now;
    UINT64 Dt;
    UINT32 Dx;
    UINT32 Dy;

    Hit = -1;
    for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
        if (PointInIcon(&gIcons[i], X, Y)) {
            Hit = i;
            break;
        }
    }

    if (Hit < 0) {
        if (gSelected >= 0) {
            Prev = gSelected;
            gSelected = -1;
            RedrawIconIndex(Prev);
        }
        return 0;
    }

    Now = DesktopClock();
    Dt = (Now >= gSelectClock) ? (Now - gSelectClock) : DESKTOP_DBLCLICK_MAX + 1;
    Dx = (X >= gSelectX) ? (X - gSelectX) : (gSelectX - X);
    Dy = (Y >= gSelectY) ? (Y - gSelectY) : (gSelectY - Y);

    if (Hit == gSelected &&
        Dt <= DESKTOP_DBLCLICK_MAX &&
        Dx <= DESKTOP_DBLCLICK_SLOP &&
        Dy <= DESKTOP_DBLCLICK_SLOP) {
        gSelected = -1;
        RedrawIconIndex(Hit);
        DebugWrite("desktop: double-click ");
        DebugWrite(gIcons[Hit].Label);
        DebugWrite("\n");
        OpenAction(gIcons[Hit].Action);
        return 1;
    }

    /* 首次点、点太慢、或移开太远：只选中 */
    SelectIcon(Hit, X, Y, Now);
    return 1;
}
