/*
 * Gui.c — 桌面、窗口、光标绘制
 *
 * 光标：关中断 → 恢复旧像素 → 保存新位置 → 绘制十字。
 * 避免 XOR / 局部重绘在抢占下留下轨迹。
 */
#include "Gui.h"
#include "Serial.h"
#include "UI.h"
#include "Video.h"
#include "Hal.h"
#include "Debug.h"

#define DRAG_MIN_STEP 3

#define MAX_WINS     4
#define TITLE_HEIGHT GUI_TITLE_HEIGHT
#define CLOSE_SIZE   24
#define CLOSE_MARGIN 6
#define CURSOR_HALF  6
#define CURSOR_BOX   (CURSOR_HALF * 2 + 1)

typedef struct {
    int      Active;
    UINT32   X;
    UINT32   Y;
    UINT32   Width;
    UINT32   Height;
    UINT32   Background;
    const char *Title;
    UINT32   TermX;
    UINT32   TermY;
    int      TermSet;
} GUI_WINDOW;

static GUI_WINDOW gWins[MAX_WINS];
static UINT32 gScreenW;
static UINT32 gScreenH;
static UINT32 gCursorX;
static UINT32 gCursorY;
static UINT8  gCursorBtn;
static int    gFocusWin;

static UINT32 gSaveX;
static UINT32 gSaveY;
static UINT32 gSaveW;
static UINT32 gSaveH;
static UINT32 gUnder[CURSOR_BOX * CURSOR_BOX];
static int    gCursorVisible;

/* 标题栏拖动：按下时记录窗口下标与光标相对偏移 */
static int    gDragWin = -1;
static INT32  gDragOffX;
static INT32  gDragOffY;

/* 保存进入绘制区前的 IF，避免在中断/异常里 ConsoleWrite 后误 sti 嵌套中断 */
static int    gGfxLockDepth;
static int    gGfxHadIrq;

static void GfxIrqEnter(void) {
    if (gGfxLockDepth++ == 0) {
        UINT64 Flags;
        __asm__ volatile ("pushfq; pop %0" : "=r"(Flags) :: "memory");
        gGfxHadIrq = (Flags & (1ULL << 9)) != 0;
        HalIrqDisable();
    }
}

static void GfxIrqLeave(void) {
    if (gGfxLockDepth > 0 && --gGfxLockDepth == 0 && gGfxHadIrq) {
        HalIrqEnable();
    }
}

static void DrawWindowChromeAt(int Idx);
static void DrawWindowAt(int Idx);
static void RedrawWindowChrome(void);
static void RefreshOtherChrome(int SkipIdx);
static void CursorRestore(void);
static void CursorPaint(void);
static void SyncWindowVisuals(void);
void GuiFocusApply(void);
void GuiRedraw(void);

static UINT32 TitleBarColor(int Idx) {
    if (Idx == gFocusWin && gWins[Idx].Active) {
        return COLOR_BLUE;
    }
    return COLOR_GRAY;
}

static void CloseButtonRect(const GUI_WINDOW *W, UINT32 *Bx, UINT32 *By,
                            UINT32 *Bw, UINT32 *Bh) {
    *Bw = CLOSE_SIZE;
    *Bh = CLOSE_SIZE;
    *Bx = W->X + W->Width - *Bw - CLOSE_MARGIN;
    *By = W->Y + (TITLE_HEIGHT - *Bh) / 2;
}

static int PointInClose(const GUI_WINDOW *W, UINT32 X, UINT32 Y) {
    UINT32 Bx;
    UINT32 By;
    UINT32 Bw;
    UINT32 Bh;

    if (!W->Active) {
        return 0;
    }
    CloseButtonRect(W, &Bx, &By, &Bw, &Bh);
    return X >= Bx && X < Bx + Bw && Y >= By && Y < By + Bh;
}

static void DrawCloseButton(const GUI_WINDOW *W) {
    UINT32 Bx;
    UINT32 By;
    UINT32 Bw;
    UINT32 Bh;
    UINT32 Pad;

    CloseButtonRect(W, &Bx, &By, &Bw, &Bh);
    UiFillRectangle(Bx, By, Bw, Bh, COLOR_RED);
    UiDrawRectangle(Bx, By, Bw, Bh, COLOR_WHITE);
    /* 字体为 16×32，24×24 按钮内放不下；用对角线画居中 × */
    Pad = 7;
    if (Bw > Pad * 2 + 2 && Bh > Pad * 2 + 2) {
        UiDrawLine(Bx + Pad, By + Pad, Bx + Bw - 1 - Pad, By + Bh - 1 - Pad,
                   COLOR_WHITE);
        UiDrawLine(Bx + Bw - 1 - Pad, By + Pad, Bx + Pad, By + Bh - 1 - Pad,
                   COLOR_WHITE);
    }
}

static int RectsOverlap(const GUI_WINDOW *A, const GUI_WINDOW *B) {
    if (!A->Active || !B->Active) {
        return 0;
    }
    return A->X < B->X + B->Width && B->X < A->X + A->Width &&
           A->Y < B->Y + B->Height && B->Y < A->Y + A->Height;
}

static int RectOverlapsAnyWindow(UINT32 X, UINT32 Y, UINT32 Ww, UINT32 Wh,
                                 int SkipIdx) {
    GUI_WINDOW Probe;
    int j;

    Probe.Active = 1;
    Probe.X = X;
    Probe.Y = Y;
    Probe.Width = Ww;
    Probe.Height = Wh;
    for (j = 0; j < MAX_WINS; j++) {
        if (j != SkipIdx && RectsOverlap(&Probe, &gWins[j])) {
            return 1;
        }
    }
    return 0;
}

static void RefreshOtherChrome(int SkipIdx) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (gWins[i].Active && i != SkipIdx) {
            DrawWindowChromeAt(i);
        }
    }
}

static void SyncWindowVisuals(void) {
    RedrawWindowChrome();
}

static void CloseWindow(int Idx) {
    UINT32 X;
    UINT32 Y;
    UINT32 Ww;
    UINT32 Wh;
    int i;

    if (Idx < 0 || Idx >= MAX_WINS || !gWins[Idx].Active) {
        return;
    }
    X = gWins[Idx].X;
    Y = gWins[Idx].Y;
    Ww = gWins[Idx].Width;
    Wh = gWins[Idx].Height;
    gWins[Idx].Active = 0;
    gWins[Idx].TermSet = 0;
    if (gDragWin == Idx) {
        gDragWin = -1;
    }
    if (gFocusWin == Idx) {
        gFocusWin = -1;
        for (i = MAX_WINS - 1; i >= 0; i--) {
            if (gWins[i].Active) {
                gFocusWin = i;
                break;
            }
        }
    }
    HalIrqDisable();
    CursorRestore();
    UiFillRectangle(X, Y, Ww, Wh, COLOR_DARK_GRAY);
    for (i = 0; i < MAX_WINS; i++) {
        if (gWins[i].Active) {
            DrawWindowChromeAt(i);
        }
    }
    CursorPaint();
    HalIrqEnable();
    GuiFocusApply();
    DebugWrite("gui: closed window\n");
}

static void CursorBox(UINT32 Cx, UINT32 Cy, UINT32 *Sx, UINT32 *Sy,
                      UINT32 *Sw, UINT32 *Sh) {
    *Sx = Cx >= CURSOR_HALF ? Cx - CURSOR_HALF : 0;
    *Sy = Cy >= CURSOR_HALF ? Cy - CURSOR_HALF : 0;
    UINT32 Ex = Cx + CURSOR_HALF + 1;
    UINT32 Ey = Cy + CURSOR_HALF + 1;
    if (Ex > gScreenW) {
        Ex = gScreenW;
    }
    if (Ey > gScreenH) {
        Ey = gScreenH;
    }
    *Sw = Ex - *Sx;
    *Sh = Ey - *Sy;
}

static void DrawCursorAt(UINT32 X, UINT32 Y) {
    int i;

    for (i = -CURSOR_HALF; i <= CURSOR_HALF; i++) {
        int Px = (int)X + i;
        int Py = (int)Y + i;
        if (Px >= 0 && (UINT32)Px < gScreenW) {
            VideoDrawPixel((UINT32)Px, Y, COLOR_WHITE);
        }
        if (Py >= 0 && (UINT32)Py < gScreenH) {
            VideoDrawPixel(X, (UINT32)Py, COLOR_WHITE);
        }
    }
    VideoDrawPixel(X, Y, COLOR_RED);
}

static void CursorRestore(void) {
    UINT32 Dy;
    UINT32 Dx;

    if (!gCursorVisible) {
        return;
    }
    for (Dy = 0; Dy < gSaveH; Dy++) {
        for (Dx = 0; Dx < gSaveW; Dx++) {
            VideoDrawPixel(gSaveX + Dx, gSaveY + Dy,
                      gUnder[Dy * gSaveW + Dx]);
        }
    }
    gCursorVisible = 0;
}

static void CursorPaint(void) {
    UINT32 Dy;
    UINT32 Dx;

    CursorBox(gCursorX, gCursorY, &gSaveX, &gSaveY, &gSaveW, &gSaveH);
    for (Dy = 0; Dy < gSaveH; Dy++) {
        for (Dx = 0; Dx < gSaveW; Dx++) {
            gUnder[Dy * gSaveW + Dx] =
                VideoReadPixel(gSaveX + Dx, gSaveY + Dy);
        }
    }
    DrawCursorAt(gCursorX, gCursorY);
    gCursorVisible = 1;
}

static void CursorMove(UINT32 X, UINT32 Y) {
    if (X >= gScreenW) {
        X = gScreenW > 0 ? gScreenW - 1 : 0;
    }
    if (Y >= gScreenH) {
        Y = gScreenH > 0 ? gScreenH - 1 : 0;
    }
    if (X == gCursorX && Y == gCursorY) {
        return;
    }

    /* 拖动时只跟踪坐标，在 MoveWindowTo 里统一重画光标，避免十字像素被拷进窗口 */
    if (gDragWin >= 0) {
        gCursorX = X;
        gCursorY = Y;
        return;
    }

    HalIrqDisable();
    CursorRestore();
    gCursorX = X;
    gCursorY = Y;
    CursorPaint();
    HalIrqEnable();
}

static void DrawWindowAt(int Idx) {
    const GUI_WINDOW *W = &gWins[Idx];

    if (!W->Active) {
        return;
    }
    UiFillRectangle(W->X, W->Y, W->Width, TITLE_HEIGHT, TitleBarColor(Idx));
    UiDrawRectangle(W->X, W->Y, W->Width, W->Height, COLOR_WHITE);
    UiFillRectangle(W->X + 2, W->Y + TITLE_HEIGHT, W->Width - 4,
                    W->Height - TITLE_HEIGHT - 2, W->Background);
    VideoDrawStringAt(W->X + 8, W->Y + 4, W->Title, COLOR_WHITE);
    DrawCloseButton(W);
}

/* 仅重绘标题栏与边框，保留客户区已有文字 */
static void DrawWindowChromeAt(int Idx) {
    const GUI_WINDOW *W = &gWins[Idx];

    if (!W->Active) {
        return;
    }
    UiFillRectangle(W->X, W->Y, W->Width, TITLE_HEIGHT, TitleBarColor(Idx));
    UiDrawRectangle(W->X, W->Y, W->Width, W->Height, COLOR_WHITE);
    VideoDrawStringAt(W->X + 8, W->Y + 4, W->Title, COLOR_WHITE);
    DrawCloseButton(W);
}

static void RedrawWindowChrome(void) {
    int i;
    int Last = gFocusWin;

    HalIrqDisable();
    CursorRestore();
    for (i = 0; i < MAX_WINS; i++) {
        if (gWins[i].Active && i != Last) {
            DrawWindowChromeAt(i);
        }
    }
    if (Last >= 0 && Last < MAX_WINS && gWins[Last].Active) {
        DrawWindowChromeAt(Last);
    }
    CursorPaint();
    HalIrqEnable();
}

static int PointInWindow(const GUI_WINDOW *W, UINT32 X, UINT32 Y) {
    return W->Active && X >= W->X && X < W->X + W->Width &&
           Y >= W->Y && Y < W->Y + W->Height;
}

static int PointInTitle(const GUI_WINDOW *W, UINT32 X, UINT32 Y) {
    return W->Active && X >= W->X && X < W->X + W->Width &&
           Y >= W->Y && Y < W->Y + TITLE_HEIGHT;
}

/* 用桌面色填「旧矩形里未被新矩形覆盖」的区域 */
static void FillUncovered(INT32 Ox, INT32 Oy, INT32 Nx, INT32 Ny,
                          INT32 Ww, INT32 Wh) {
    INT32 OldR = Ox + Ww;
    INT32 OldB = Oy + Wh;
    INT32 NewR = Nx + Ww;
    INT32 NewB = Ny + Wh;

    if (Ny > Oy) {
        UiFillRectangle((UINT32)Ox, (UINT32)Oy, (UINT32)Ww, (UINT32)(Ny - Oy),
                        COLOR_DARK_GRAY);
    }
    if (NewB < OldB) {
        UiFillRectangle((UINT32)Ox, (UINT32)NewB, (UINT32)Ww, (UINT32)(OldB - NewB),
                        COLOR_DARK_GRAY);
    }
    {
        INT32 Y0 = (Ny > Oy) ? Ny : Oy;
        INT32 Y1 = (NewB < OldB) ? NewB : OldB;
        if (Y1 > Y0) {
            if (Nx > Ox) {
                UiFillRectangle((UINT32)Ox, (UINT32)Y0, (UINT32)(Nx - Ox),
                                (UINT32)(Y1 - Y0), COLOR_DARK_GRAY);
            }
            if (NewR < OldR) {
                UiFillRectangle((UINT32)NewR, (UINT32)Y0, (UINT32)(OldR - NewR),
                                (UINT32)(Y1 - Y0), COLOR_DARK_GRAY);
            }
        }
    }
}

/* 窗口必须完整留在屏内 */
static void ClampWindowPos(const GUI_WINDOW *W, INT32 *X, INT32 *Y) {
    INT32 MaxX = (INT32)gScreenW - (INT32)W->Width;
    INT32 MaxY = (INT32)gScreenH - (INT32)W->Height;

    if (MaxX < 0) {
        MaxX = 0;
    }
    if (MaxY < 0) {
        MaxY = 0;
    }
    if (*X < 0) {
        *X = 0;
    }
    if (*Y < 0) {
        *Y = 0;
    }
    if (*X > MaxX) {
        *X = MaxX;
    }
    if (*Y > MaxY) {
        *Y = MaxY;
    }
}

static int PointOnAnyClose(UINT32 X, UINT32 Y) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (gWins[i].Active && PointInClose(&gWins[i], X, Y)) {
            return 1;
        }
    }
    return 0;
}

static void MoveWindowTo(int Idx, UINT32 NewX, UINT32 NewY) {
    GUI_WINDOW *W;
    UINT32 Ox;
    UINT32 Oy;
    UINT32 Ww;
    UINT32 Wh;
    INT32 Dx;
    INT32 Dy;

    if (Idx < 0 || Idx >= MAX_WINS) {
        return;
    }
    W = &gWins[Idx];
    if (!W->Active) {
        return;
    }
    Ox = W->X;
    Oy = W->Y;
    Ww = W->Width;
    Wh = W->Height;
    if (NewX == Ox && NewY == Oy) {
        return;
    }
    if (NewX + Ww > gScreenW || NewY + Wh > gScreenH) {
        return;
    }

    Dx = (INT32)NewX - (INT32)Ox;
    Dy = (INT32)NewY - (INT32)Oy;

    HalIrqDisable();
    CursorRestore();
    if (RectOverlapsAnyWindow(NewX, NewY, Ww, Wh, Idx)) {
        FillUncovered((INT32)Ox, (INT32)Oy, (INT32)NewX, (INT32)NewY,
                      (INT32)Ww, (INT32)Wh);
        W->X = NewX;
        W->Y = NewY;
        if (W->TermSet) {
            W->TermX = (UINT32)((INT32)W->TermX + Dx);
            W->TermY = (UINT32)((INT32)W->TermY + Dy);
        }
        RefreshOtherChrome(Idx);
        DrawWindowAt(Idx);
    } else {
        VideoCopyRect(Ox, Oy, NewX, NewY, Ww, Wh);
        FillUncovered((INT32)Ox, (INT32)Oy, (INT32)NewX, (INT32)NewY,
                      (INT32)Ww, (INT32)Wh);
        W->X = NewX;
        W->Y = NewY;
        if (W->TermSet) {
            W->TermX = (UINT32)((INT32)W->TermX + Dx);
            W->TermY = (UINT32)((INT32)W->TermY + Dy);
        }
        RefreshOtherChrome(Idx);
        DrawWindowChromeAt(Idx);
    }
    CursorPaint();
    HalIrqEnable();
}

/* 将窗口移到最前（数组后部 = 绘制在上层） */
static void RaiseWindow(int Idx) {
    int Top = Idx;
    int J;

    for (J = Idx + 1; J < MAX_WINS; J++) {
        if (gWins[J].Active) {
            Top = J;
        }
    }
    if (Top == Idx) {
        gFocusWin = Idx;
        return;
    }
    {
        GUI_WINDOW Tmp = gWins[Idx];
        for (J = Idx; J < Top; J++) {
            gWins[J] = gWins[J + 1];
        }
        gWins[Top] = Tmp;
        gFocusWin = Top;
    }
}

int GuiFocusClient(UINT32 *X, UINT32 *Y, UINT32 *Width, UINT32 *Height, UINT32 *Background) {
    const GUI_WINDOW *Win;
    UINT32 Pad = GUI_CLIENT_PAD;

    if (gFocusWin < 0 || gFocusWin >= MAX_WINS || !gWins[gFocusWin].Active) {
        return 0;
    }
    Win = &gWins[gFocusWin];
    if (X) {
        *X = Win->X + 2 + Pad;
    }
    if (Y) {
        *Y = Win->Y + TITLE_HEIGHT + Pad;
    }
    if (Width) {
        *Width = (Win->Width > 4 + Pad * 2) ? Win->Width - 4 - Pad * 2 : 0;
    }
    if (Height) {
        *Height = (Win->Height > TITLE_HEIGHT + 2 + Pad * 2) ?
             Win->Height - TITLE_HEIGHT - 2 - Pad * 2 : 0;
    }
    if (Background) {
        *Background = Win->Background;
    }
    if (Width && Height) {
        return *Width > 0 && *Height > 0;
    }
    return 1;
}

void GuiFocusSave(void) {
    if (gFocusWin < 0 || gFocusWin >= MAX_WINS || !gWins[gFocusWin].Active) {
        return;
    }
    VideoGetTextCursor(&gWins[gFocusWin].TermX, &gWins[gFocusWin].TermY);
    gWins[gFocusWin].TermSet = 1;
}

void GuiFocusApply(void) {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Bg;
    GUI_WINDOW *Win;

    if (!GuiFocusClient(&X, &Y, &W, &H, &Bg)) {
        VideoClearClip();
        return;
    }
    VideoSetClipRegion(X, Y, W, H, Bg);
    Win = &gWins[gFocusWin];
    if (Win->TermSet &&
        Win->TermX >= X && Win->TermY >= Y &&
        Win->TermX < X + W && Win->TermY < Y + H) {
        VideoSetTextCursor(Win->TermX, Win->TermY);
    } else {
        VideoSetTextCursor(X, Y);
    }
}

void GuiFocusHome(void) {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Bg;
    GUI_WINDOW *Win;

    if (!GuiFocusClient(&X, &Y, &W, &H, &Bg)) {
        VideoClearClip();
        return;
    }
    VideoSetClipOrigin(X, Y, W, H, Bg);
    if (gFocusWin >= 0 && gFocusWin < MAX_WINS) {
        Win = &gWins[gFocusWin];
        Win->TermX = X;
        Win->TermY = Y;
        Win->TermSet = 1;
    }
}

void GuiRedraw(void) {
    int i;

    GfxIrqEnter();
    CursorRestore();
    UiFillRectangle(0, 0, gScreenW, gScreenH, COLOR_DARK_GRAY);
    for (i = 0; i < MAX_WINS; i++) {
        DrawWindowAt(i);
    }
    CursorPaint();
    GfxIrqLeave();
}

void GuiInit(void) {
    VideoGetSize(&gScreenW, &gScreenH);
    if (gScreenW == 0) {
        gScreenW = 1024;
        gScreenH = 768;
    }
    gCursorX = gScreenW / 2;
    gCursorY = gScreenH / 2;
    gFocusWin = 0;
    gCursorVisible = 0;
    gDragWin = -1;

    {
        /* 略小于全屏，露出桌面，方便演示拖动 */
        UINT32 Margin = 48;
        UINT32 ShellW = gScreenW > Margin * 2 + 200 ?
                        gScreenW - Margin * 2 : gScreenW - 32;
        UINT32 ShellH = gScreenH > Margin * 2 + 120 ?
                        gScreenH - Margin * 2 : gScreenH - 32;

        UINT32 Gap = 32;
        UINT32 HalfW = (ShellW > Gap) ? (ShellW - Gap) / 2 : ShellW / 2;

        gWins[0].Active = 1;
        gWins[0].X = Margin;
        gWins[0].Y = Margin;
        gWins[0].Width = HalfW;
        gWins[0].Height = ShellH;
        gWins[0].Background = COLOR_LIGHT_GRAY;
        gWins[0].Title = "ToyOS Shell";
        gWins[0].TermSet = 0;

        gWins[1].Active = 1;
        gWins[1].X = Margin + HalfW + Gap;
        gWins[1].Y = Margin;
        gWins[1].Width = HalfW;
        gWins[1].Height = ShellH * 2 / 3;
        gWins[1].Background = COLOR_LIGHT_GRAY;
        gWins[1].Title = "Shell 2";
        gWins[1].TermSet = 0;

        gWins[2].Active = 0;
        gWins[3].Active = 0;
    }

    gFocusWin = 0;
    GuiRedraw();
    DebugWrite("gui: desktop ready (2 shell windows)\n");
}

void GuiOnArrowKey(UINT8 Key) {
    UINT32 X = gCursorX;
    UINT32 Y = gCursorY;
    UINT32 Step = 8;

    if (Key == 0x50 && X >= Step) {
        X -= Step;
    } else if (Key == 0x4F && X + Step < gScreenW) {
        X += Step;
    } else if (Key == 0x52 && Y >= Step) {
        Y -= Step;
    } else if (Key == 0x51 && Y + Step < gScreenH) {
        Y += Step;
    } else if (Key == 0x28) {
        GUI_MOUSE_STATE M;
        M.X = gCursorX;
        M.Y = gCursorY;
        M.Buttons = 1;
        GuiOnMouse(&M);
        M.Buttons = 0;
        GuiOnMouse(&M);
        return;
    } else {
        return;
    }
    CursorMove(X, Y);
}

int GuiHandleClick(UINT32 X, UINT32 Y) {
    int i;

    /* 关闭钮可能被其它窗口挡住；先扫一遍所有窗口的 × 区域 */
    for (i = MAX_WINS - 1; i >= 0; i--) {
        if (gWins[i].Active && PointInClose(&gWins[i], X, Y)) {
            CloseWindow(i);
            return 1;
        }
    }

    for (i = MAX_WINS - 1; i >= 0; i--) {
        if (!PointInWindow(&gWins[i], X, Y)) {
            continue;
        }
        GuiFocusSave();
        RaiseWindow(i);
        SyncWindowVisuals();
        GuiFocusApply();
        DebugWrite("gui: focus ");
        DebugWrite(gWins[gFocusWin].Title);
        DebugWrite("\n");

        if (PointInTitle(&gWins[gFocusWin], X, Y) &&
            !PointOnAnyClose(X, Y)) {
            HalIrqDisable();
            CursorRestore();
            HalIrqEnable();
            RaiseWindow(gFocusWin);
            gDragWin = gFocusWin;
            gDragOffX = (INT32)X - (INT32)gWins[gFocusWin].X;
            gDragOffY = (INT32)Y - (INT32)gWins[gFocusWin].Y;
        }
        return 1;
    }
    return 0;
}

static void GuiDragUpdate(UINT32 X, UINT32 Y) {
    INT32 Nx;
    INT32 Ny;
    INT32 Dx;
    INT32 Dy;
    GUI_WINDOW *W;

    if (gDragWin < 0 || gDragWin >= MAX_WINS || !gWins[gDragWin].Active) {
        return;
    }
    W = &gWins[gDragWin];
    Nx = (INT32)X - gDragOffX;
    Ny = (INT32)Y - gDragOffY;
    ClampWindowPos(W, &Nx, &Ny);

    Dx = Nx - (INT32)W->X;
    Dy = Ny - (INT32)W->Y;
    if (Dx < 0) {
        Dx = -Dx;
    }
    if (Dy < 0) {
        Dy = -Dy;
    }
    if ((UINT32)Dx < DRAG_MIN_STEP && (UINT32)Dy < DRAG_MIN_STEP) {
        return;
    }
    MoveWindowTo(gDragWin, (UINT32)Nx, (UINT32)Ny);
}

static void GuiDragEnd(void) {
    int DragIdx = gDragWin;

    if (DragIdx >= 0 && gWins[DragIdx].Active) {
        INT32 Nx = (INT32)gCursorX - gDragOffX;
        INT32 Ny = (INT32)gCursorY - gDragOffY;

        ClampWindowPos(&gWins[DragIdx], &Nx, &Ny);
        MoveWindowTo(DragIdx, (UINT32)Nx, (UINT32)Ny);
        RaiseWindow(DragIdx);
        RedrawWindowChrome();
        if (gFocusWin >= 0) {
            GuiFocusApply();
        }
        HalIrqDisable();
        CursorPaint();
        HalIrqEnable();
    }
    gDragWin = -1;
}

void GuiPointerMove(UINT32 X, UINT32 Y) {
    CursorMove(X, Y);
    if (gDragWin >= 0 && (gCursorBtn & 1)) {
        GuiDragUpdate(X, Y);
    }
}

/* 帧缓冲绘制前：关中断并擦掉光标（避免 save-under 采到十字像素） */
void GuiFrameBufferBegin(void) {
    GfxIrqEnter();
    CursorRestore();
}

/* 帧缓冲绘制后：重画光标；仅恢复进入 Begin 前已开启的中断 */
void GuiFrameBufferEnd(void) {
    CursorPaint();
    GfxIrqLeave();
}

void GuiCursorPaint(void) {
    GfxIrqEnter();
    CursorRestore();
    CursorPaint();
    GfxIrqLeave();
}

void GuiCursorHide(void) {
    GfxIrqEnter();
    CursorRestore();
    GfxIrqLeave();
}

void GuiCursorShow(void) {
    GfxIrqEnter();
    CursorPaint();
    GfxIrqLeave();
}

void GuiOnMouse(const GUI_MOUSE_STATE *Mouse) {
    static UINT8 PrevBtn;

    gCursorBtn = Mouse->Buttons;
    GuiPointerMove(Mouse->X, Mouse->Y);

    if ((Mouse->Buttons & 1) && !(PrevBtn & 1)) {
        GuiHandleClick(gCursorX, gCursorY);
    }
    if (!(Mouse->Buttons & 1) && (PrevBtn & 1)) {
        GuiDragEnd();
    }
    PrevBtn = Mouse->Buttons;
}

/* 从 XHCI 鼠标队列取报告并更新光标（Shell/Gui 任务均可调用） */
void GuiPollMouse(void) {
    HAL_MOUSE_REPORT Raw;
    UINT32 Sw;
    UINT32 Sh;
    static UINT8 PrevBtn;

    if (!HalMousePresent()) {
        return;
    }

    VideoGetSize(&Sw, &Sh);
    if (Sw == 0) {
        Sw = gScreenW ? gScreenW : 1024;
    }
    if (Sh == 0) {
        Sh = gScreenH ? gScreenH : 768;
    }

    HalInputPoll();

    while (HalMouseDequeue(&Raw)) {
        UINT32 X;
        UINT32 Y;

        if (Raw.X > Sw || Raw.Y > Sh) {
            X = Raw.X * Sw / 32767;
            Y = Raw.Y * Sh / 32767;
        } else {
            X = Raw.X;
            Y = Raw.Y;
        }
        if (X >= Sw) {
            X = Sw > 0 ? Sw - 1 : 0;
        }
        if (Y >= Sh) {
            Y = Sh > 0 ? Sh - 1 : 0;
        }

        gCursorBtn = Raw.Buttons;
        CursorMove(X, Y);

        if ((Raw.Buttons & 1) && !(PrevBtn & 1)) {
            GuiHandleClick(X, Y);
        } else if ((Raw.Buttons & 1) && gDragWin >= 0) {
            GuiDragUpdate(X, Y);
        }
        if (!(Raw.Buttons & 1) && (PrevBtn & 1)) {
            GuiDragEnd();
        }
        PrevBtn = Raw.Buttons;
    }
}
