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
#include "XHCI.h"
#include "hal.h"

#define MAX_WINS     4
#define TITLE_H      GUI_TITLE_H
#define CURSOR_HALF  6
#define CURSOR_BOX   (CURSOR_HALF * 2 + 1)

typedef struct {
    int      Active;
    UINT32   X;
    UINT32   Y;
    UINT32   W;
    UINT32   H;
    UINT32   Bg;
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
            DrawPixel((UINT32)Px, Y, COLOR_WHITE);
        }
        if (Py >= 0 && (UINT32)Py < gScreenH) {
            DrawPixel(X, (UINT32)Py, COLOR_WHITE);
        }
    }
    DrawPixel(X, Y, COLOR_RED);
}

static void CursorRestore(void) {
    UINT32 Dy;
    UINT32 Dx;

    if (!gCursorVisible) {
        return;
    }
    for (Dy = 0; Dy < gSaveH; Dy++) {
        for (Dx = 0; Dx < gSaveW; Dx++) {
            DrawPixel(gSaveX + Dx, gSaveY + Dy,
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

    HalIrqDisable();
    CursorRestore();
    gCursorX = X;
    gCursorY = Y;
    CursorPaint();
    HalIrqEnable();
}

static void DrawWindow(const GUI_WINDOW *W) {
    if (!W->Active) {
        return;
    }
    FillRectangle(W->X, W->Y, W->W, TITLE_H, COLOR_BLUE);
    DrawRectangle(W->X, W->Y, W->W, W->H, COLOR_WHITE);
    FillRectangle(W->X + 2, W->Y + TITLE_H, W->W - 4, W->H - TITLE_H - 2, W->Bg);
    DrawStringAt(W->X + 8, W->Y + 4, W->Title, COLOR_WHITE);
}

/* 仅重绘标题栏与边框，保留客户区已有文字 */
static void DrawWindowChrome(const GUI_WINDOW *W) {
    if (!W->Active) {
        return;
    }
    FillRectangle(W->X, W->Y, W->W, TITLE_H, COLOR_BLUE);
    DrawRectangle(W->X, W->Y, W->W, W->H, COLOR_WHITE);
    DrawStringAt(W->X + 8, W->Y + 4, W->Title, COLOR_WHITE);
}

static void RedrawWindowChrome(void) {
    int i;

    HalIrqDisable();
    CursorRestore();
    for (i = 0; i < MAX_WINS; i++) {
        DrawWindowChrome(&gWins[i]);
    }
    CursorPaint();
    HalIrqEnable();
}

static int PointInWin(const GUI_WINDOW *W, UINT32 X, UINT32 Y) {
    return W->Active && X >= W->X && X < W->X + W->W &&
           Y >= W->Y && Y < W->Y + W->H;
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

int GuiFocusClient(UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H, UINT32 *Bg) {
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
        *Y = Win->Y + TITLE_H + Pad;
    }
    if (W) {
        *W = (Win->W > 4 + Pad * 2) ? Win->W - 4 - Pad * 2 : 0;
    }
    if (H) {
        *H = (Win->H > TITLE_H + 2 + Pad * 2) ?
             Win->H - TITLE_H - 2 - Pad * 2 : 0;
    }
    if (Bg) {
        *Bg = Win->Bg;
    }
    if (W && H) {
        return *W > 0 && *H > 0;
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

    HalIrqDisable();
    CursorRestore();
    FillRectangle(0, 0, gScreenW, gScreenH, COLOR_DARK_GRAY);
    for (i = 0; i < MAX_WINS; i++) {
        DrawWindow(&gWins[i]);
    }
    CursorPaint();
    HalIrqEnable();
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

    {
        UINT32 Margin = 12;
        UINT32 SideW = 200;
        UINT32 ShellW = gScreenW > SideW + Margin * 3 ?
                        gScreenW - SideW - Margin * 2 : gScreenW - Margin * 2;
        UINT32 ShellH = gScreenH > Margin * 2 ? gScreenH - Margin * 2 : gScreenH;

        gWins[0].Active = 1;
        gWins[0].X = Margin;
        gWins[0].Y = Margin;
        gWins[0].W = ShellW;
        gWins[0].H = ShellH;
        gWins[0].Bg = COLOR_LIGHT_GRAY;
        gWins[0].Title = "ToyOS Shell";
        gWins[0].TermSet = 0;

        gWins[1].Active = 1;
        gWins[1].X = Margin + ShellW + Margin;
        gWins[1].Y = Margin;
        gWins[1].W = SideW;
        gWins[1].H = 110;
        gWins[1].Bg = 0x00304060;
        gWins[1].Title = "About";
        gWins[1].TermSet = 0;

        gWins[2].Active = 1;
        gWins[2].X = Margin + ShellW + Margin;
        gWins[2].Y = Margin + 120;
        gWins[2].W = SideW;
        gWins[2].H = 180;
        gWins[2].Bg = 0x00403050;
        gWins[2].Title = "Processes";
        gWins[2].TermSet = 0;
    }

    GuiRedraw();
    SerialWrite("gui: desktop ready (mouse + windows)\n");
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

    for (i = MAX_WINS - 1; i >= 0; i--) {
        if (PointInWin(&gWins[i], X, Y)) {
            GuiFocusSave();
            RaiseWindow(i);
            RedrawWindowChrome();
            GuiFocusApply();
            SerialWrite("gui: focus ");
            SerialWrite(gWins[gFocusWin].Title);
            SerialWrite("\n");
            return 1;
        }
    }
    return 0;
}

void GuiPointerMove(UINT32 X, UINT32 Y) {
    CursorMove(X, Y);
}

/* 帧缓冲绘制前：关中断并擦掉光标（避免 save-under 采到十字像素） */
void GuiFbBegin(void) {
    HalIrqDisable();
    CursorRestore();
}

/* 帧缓冲绘制后：重画光标并开中断 */
void GuiFbEnd(void) {
    CursorPaint();
    HalIrqEnable();
}

void GuiCursorPaint(void) {
    HalIrqDisable();
    CursorRestore();
    CursorPaint();
    HalIrqEnable();
}

void GuiCursorHide(void) {
    HalIrqDisable();
    CursorRestore();
    HalIrqEnable();
}

void GuiCursorShow(void) {
    HalIrqDisable();
    CursorPaint();
    HalIrqEnable();
}

void GuiOnMouse(const GUI_MOUSE_STATE *Mouse) {
    static UINT8 PrevBtn;

    GuiPointerMove(Mouse->X, Mouse->Y);
    gCursorBtn = Mouse->Buttons;

    if ((Mouse->Buttons & 1) && !(PrevBtn & 1)) {
        GuiHandleClick(gCursorX, gCursorY);
    }
    PrevBtn = Mouse->Buttons;
}

/* 从 XHCI 鼠标队列取报告并更新光标（Shell/Gui 任务均可调用） */
void GuiPollMouse(void) {
    USB_MOUSE_REPORT Raw;
    UINT32 Sw;
    UINT32 Sh;
    static UINT8 PrevBtn;

    if (!XHCIMousePresent()) {
        return;
    }

    VideoGetSize(&Sw, &Sh);
    if (Sw == 0) {
        Sw = gScreenW ? gScreenW : 1024;
    }
    if (Sh == 0) {
        Sh = gScreenH ? gScreenH : 768;
    }

    if (XHCIUsesIrq()) {
        XHCIDrainEvents();
    }

    while (XHCIDequeueMouse(&Raw)) {
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

        GuiPointerMove(X, Y);
        gCursorBtn = Raw.Buttons;
        if ((Raw.Buttons & 1) && !(PrevBtn & 1)) {
            GuiHandleClick(X, Y);
        }
        PrevBtn = Raw.Buttons;
    }
}
