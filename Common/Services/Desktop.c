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

#define DESKTOP_ICON_COUNT   3
#define DESKTOP_ICON_SIZE    48
#define DESKTOP_ICON_GAP     28
#define DESKTOP_ORIGIN_X     36
#define DESKTOP_ORIGIN_Y     36
#define DESKTOP_LABEL_PAD    6
#define DESKTOP_DBLCLICK_SLOP 16u

/*
 * 双击时限：BSP LAPIC tick（HalCpuTickInc）。
 * QEMU 上周期定时极快（可达数万～数十万/秒），400 会短到无法双击；
 * 取 2e6 约数百毫秒～数秒，覆盖常见手感。
 */
#define DESKTOP_DBLCLICK_MAX  2000000ULL

typedef enum {
    DESKTOP_ACT_SHELL = 0,
    DESKTOP_ACT_SETTINGS,
    DESKTOP_ACT_FILES
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
    return HalCpuTicks(0);
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

static void DrawOneIconRaw(const DESKTOP_ICON *Icon, int Selected) {
    UINT32 LabelX;
    UINT32 LabelY;
    UINT32 LabelW;
    const char *P;
    UINT32 Border;

    /* GuiRedraw：先画图标再画窗，无需逐像素避让（避让在关中断下会拖死 USB 鼠标） */
    UiFillRectangle(Icon->X, Icon->Y, DESKTOP_ICON_SIZE, DESKTOP_ICON_SIZE,
                    Icon->IconColor);
    Border = Selected ? COLOR_YELLOW : COLOR_WHITE;
    UiDrawRectangle(Icon->X, Icon->Y, DESKTOP_ICON_SIZE, DESKTOP_ICON_SIZE,
                    Border);
    if (Selected) {
        UiDrawRectangle(Icon->X + 1, Icon->Y + 1,
                        DESKTOP_ICON_SIZE - 2, DESKTOP_ICON_SIZE - 2,
                        COLOR_YELLOW);
    }
    UiFillRectangle(Icon->X + DESKTOP_ICON_SIZE - 14, Icon->Y,
                    14, 14, COLOR_LIGHT_GRAY);

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
        HalVideoDrawStringAt(LabelX, LabelY, Icon->Label,
                             Selected ? COLOR_YELLOW : COLOR_WHITE);
    }
}

static void DrawOneIconOccluded(const DESKTOP_ICON *Icon, int Selected) {
    UINT32 LabelX;
    UINT32 LabelY;
    UINT32 LabelW;
    const char *P;
    UINT32 Border;

    FillRectFree(Icon->X, Icon->Y, DESKTOP_ICON_SIZE, DESKTOP_ICON_SIZE,
                 Icon->IconColor);
    Border = Selected ? COLOR_YELLOW : COLOR_WHITE;
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

void DesktopDraw(void) {
    int i;

    for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
        DrawOneIconRaw(&gIcons[i], i == gSelected);
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
            DrawOneIconOccluded(&gIcons[i], i == gSelected);
        }
    }
}

/*
 * 拖窗 under-drag / 合成采样：方块 + 边框 + 角标 + 标签字形（与 DrawOneIcon 几何一致）。
 * 命中任一不透明图标像素返回 1；否则 0（调用方用桌面底色）。
 */
int DesktopSamplePixel(UINT32 X, UINT32 Y, UINT32 *Out) {
    int i;
    UINT32 Border;
    UINT32 LabelW;
    UINT32 LabelX;
    UINT32 LabelY;
    const char *P;
    UINT32 Cx;
    UINT32 AdvX;
    UINT32 CellW;
    UINT32 CellH;
    const FONT_FACE *Face;
    UINT32 Scale;

    if (!Out) {
        return 0;
    }
    AdvX = FontAdvanceX();
    CellW = FontCellW();
    CellH = FontCellH();
    Face = FontGetCurrent();
    Scale = (Face && Face->Scale) ? Face->Scale : 1u;

    for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
        const DESKTOP_ICON *Icon = &gIcons[i];
        int Selected = (i == gSelected);
        UINT32 Ix;
        UINT32 Iy;
        UINT32 Iw;
        UINT32 Ih;

        IconBounds(Icon, &Ix, &Iy, &Iw, &Ih);
        if (X < Ix || Y < Iy || X >= Ix + Iw || Y >= Iy + Ih) {
            continue;
        }

        /* 48×48 色块 */
        if (X >= Icon->X && Y >= Icon->Y &&
            X < Icon->X + DESKTOP_ICON_SIZE &&
            Y < Icon->Y + DESKTOP_ICON_SIZE) {
            Border = Selected ? COLOR_YELLOW : COLOR_WHITE;
            if (X >= Icon->X + DESKTOP_ICON_SIZE - 14 && Y < Icon->Y + 14) {
                *Out = COLOR_LIGHT_GRAY;
                return 1;
            }
            if (X == Icon->X || X == Icon->X + DESKTOP_ICON_SIZE - 1 ||
                Y == Icon->Y || Y == Icon->Y + DESKTOP_ICON_SIZE - 1) {
                *Out = Border;
                return 1;
            }
            if (Selected &&
                (X == Icon->X + 1 || X == Icon->X + DESKTOP_ICON_SIZE - 2 ||
                 Y == Icon->Y + 1 || Y == Icon->Y + DESKTOP_ICON_SIZE - 2)) {
                *Out = COLOR_YELLOW;
                return 1;
            }
            *Out = Icon->IconColor;
            return 1;
        }

        /* 标签区：与 DrawOneIcon* 同几何，采样字形前景像素 */
        if (!Icon->Label || Face == 0) {
            continue;
        }
        LabelW = 0;
        for (P = Icon->Label; *P; P++) {
            LabelW += AdvX;
        }
        LabelX = Icon->X;
        if (LabelW < DESKTOP_ICON_SIZE) {
            LabelX = Icon->X + (DESKTOP_ICON_SIZE - LabelW) / 2;
        }
        LabelY = Icon->Y + DESKTOP_ICON_SIZE + DESKTOP_LABEL_PAD;
        if (Y < LabelY || Y >= LabelY + CellH) {
            continue;
        }
        Cx = LabelX;
        for (P = Icon->Label; *P; P++) {
            if (*P != '\n' && X >= Cx && X < Cx + AdvX) {
                const UINT8 *Glyph = FontGlyph(*P);
                UINT32 RelX = X - Cx;
                UINT32 RelY = Y - LabelY;
                UINT32 Gx;
                UINT32 Gy;

                if (Glyph != 0 && RelX < CellW && RelY < CellH) {
                    Gx = RelX / Scale;
                    Gy = RelY / Scale;
                    if (Gx < Face->Width && Gy < Face->Height) {
                        UINT8 Byte = Glyph[Gy * Face->BytesPerRow + (Gx / 8)];
                        int Bit = 7 - (int)(Gx % 8);

                        if (Byte & (1 << Bit)) {
                            *Out = Selected ? COLOR_YELLOW : COLOR_WHITE;
                            return 1;
                        }
                    }
                }
                return 0; /* 在该字单元格内但非前景 → 透出桌面 */
            }
            Cx += AdvX;
        }
    }
    return 0;
}

static void RedrawIconIndex(int Idx) {
    if (Idx < 0 || Idx >= DESKTOP_ICON_COUNT) {
        return;
    }
    /* 选中态刷新时可能已有窗口，避让写穿 */
    DrawOneIconOccluded(&gIcons[Idx], Idx == gSelected);
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
        return;
    }
    if (Action == DESKTOP_ACT_FILES) {
        (void)GuiOpenFiles();
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

    gIcons[2].Label = "Files";
    gIcons[2].Action = DESKTOP_ACT_FILES;
    gIcons[2].IconColor = 0x00208040;
    gIcons[2].X = DESKTOP_ORIGIN_X;
    gIcons[2].Y = DESKTOP_ORIGIN_Y + RowH * 2;

    gSelected = -1;
    gSelectClock = 0;
    gSelectX = 0;
    gSelectY = 0;
    DebugWrite("desktop: icons ready (double-click within ~timer ticks)\n");
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
