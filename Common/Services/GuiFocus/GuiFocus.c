/*
 * GuiFocus.c — 焦点窗裁剪与光标（核心）
 * 辅助：GuiFocusConsole.c
 *
 * 从 GuiFocus.c 单体迁出；只搬家、不改逻辑。
 */
#include "GuiPrivate.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Font.h"

int GuiFocusClient(UINT32 *X, UINT32 *Y, UINT32 *Width, UINT32 *Height, UINT32 *Background) {
    const GUI_WINDOW *Win;
    UINT32 Pad = GUI_CLIENT_PAD;

    if (gFocusWin < 0 || gFocusWin >= MAX_WINS || !gWindows[gFocusWin].Active) {
        return 0;
    }
    Win = &gWindows[gFocusWin];
    /* 与 DrawWindowAt 一致：1px 白边内侧再加 GUI_CLIENT_PAD 文本边距 */
    if (X) {
        *X = Win->X + 1 + Pad;
    }
    if (Y) {
        *Y = Win->Y + TITLE_HEIGHT + Pad;
    }
    if (Width) {
        *Width = (Win->Width > 2 + Pad * 2) ? Win->Width - 2 - Pad * 2 : 0;
    }
    if (Height) {
        *Height = (Win->Height > TITLE_HEIGHT + 1 + Pad * 2) ?
             Win->Height - TITLE_HEIGHT - 1 - Pad * 2 : 0;
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
    UINT32 Cx;
    UINT32 Cy;
    UINT32 W;
    UINT32 H;
    UINT32 Bg;
    UINT32 Ax;
    UINT32 Ay;
    GUI_WINDOW *Win;

    if (gFocusWin < 0 || gFocusWin >= MAX_WINS || !gWindows[gFocusWin].Active) {
        return;
    }
    GuiConsoleOpsFocusSave();
    Win = &gWindows[gFocusWin];
    if (!GuiFocusClient(&Cx, &Cy, &W, &H, &Bg)) {
        return;
    }
    HalVideoGetTextCursor(&Ax, &Ay);
    Win->TermX = (Ax >= Cx) ? (Ax - Cx) : 0;
    Win->TermY = (Ay >= Cy) ? (Ay - Cy) : 0;
    if (W > 0 && Win->TermX >= W) {
        Win->TermX = W - 1;
    }
    if (H > 0 && Win->TermY >= H) {
        Win->TermY = H - 1;
    }
    Win->TermSet = 1;
}

void GuiFocusApply(void) {
    GuiFocusApplyClip();
    GuiConsoleOpsFocusLoad();
}


void GuiFocusApplyClip(void) {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Bg;
    GUI_WINDOW *Win;
    UINT32 Tx;
    UINT32 Ty;

    if (!GuiFocusClient(&X, &Y, &W, &H, &Bg)) {
        HalVideoClearClip();
        return;
    }
    HalVideoSetClipRegion(X, Y, W, H, Bg);
    Win = &gWindows[gFocusWin];
    if (!Win->TermSet) {
        HalVideoSetTextCursor(X, Y);
        Win->TermX = 0;
        Win->TermY = 0;
        return;
    }
    Tx = X + Win->TermX;
    Ty = Y + Win->TermY;
    if (Tx < X) {
        Tx = X;
    }
    if (Ty < Y) {
        Ty = Y;
    }
    if (W > 0 && Tx >= X + W) {
        Tx = X + W - 1;
    }
    if (H > 0 && Ty >= Y + H) {
        Ty = Y + H - 1;
    }
    HalVideoSetTextCursor(Tx, Ty);
    Win->TermX = Tx - X;
    Win->TermY = Ty - Y;
}


void GuiFocusSyncCursor(void) {
    UINT32 Cx;
    UINT32 Cy;
    UINT32 W;
    UINT32 H;
    UINT32 Bg;
    UINT32 Ax;
    UINT32 Ay;
    GUI_WINDOW *Win;

    if (gFocusWin < 0 || gFocusWin >= MAX_WINS || !gWindows[gFocusWin].Active) {
        return;
    }
    if (!GuiFocusClient(&Cx, &Cy, &W, &H, &Bg)) {
        return;
    }
    Win = &gWindows[gFocusWin];
    HalVideoGetTextCursor(&Ax, &Ay);
    Win->TermX = (Ax >= Cx) ? (Ax - Cx) : 0;
    Win->TermY = (Ay >= Cy) ? (Ay - Cy) : 0;
    if (W > 0 && Win->TermX >= W) {
        Win->TermX = W - 1;
    }
    if (H > 0 && Win->TermY >= H) {
        Win->TermY = H - 1;
    }
    Win->TermSet = 1;
}


void GuiFocusClearClient(void) {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Bg;
    GUI_WINDOW *Win;

    if (!GuiFocusClient(&X, &Y, &W, &H, &Bg)) {
        return;
    }
    GfxIrqEnter();
    CursorRestore();
    /*
     * 整块清客户区。主题合成靠 gDeferPresent + 上层后画，勿 FillRectOccluded，
     * 否则重叠区不换色，抬窗后备份镂空。
     */
    HalVideoFillRect(X, Y, W, H, Bg);
    HalVideoSetClipOrigin(X, Y, W, H, Bg);
    Win = &gWindows[gFocusWin];
    Win->TermX = 0;
    Win->TermY = 0;
    Win->TermSet = 1;
    GuiBackupSyncRect(X, Y, W, H);
    CursorPaint();
    GfxPresent();
    GfxIrqLeave();
}

void GuiSetFocusWindow(int Idx) {
    if (Idx >= 0 && Idx < MAX_WINS && gWindows[Idx].Active) {
        gFocusWin = Idx;
    }
}


int GuiFocusIndex(void) {
    return gFocusWin;
}


void GuiFocusHome(void) {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Bg;
    GUI_WINDOW *Win;

    if (!GuiFocusClient(&X, &Y, &W, &H, &Bg)) {
        HalVideoClearClip();
        return;
    }
    HalVideoSetClipOrigin(X, Y, W, H, Bg);
    if (gFocusWin >= 0 && gFocusWin < MAX_WINS) {
        Win = &gWindows[gFocusWin];
        Win->TermX = 0;
        Win->TermY = 0;
        Win->TermSet = 1;
    }
}

