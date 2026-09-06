/*
 * GuiCursor.c — PR-R2：鼠标光标 save-under
 */
#include "GuiPriv.h"
#include "HalVideo.h"
#include "Hal.h"
#include "UI.h"
#include "FilesUi.h"

void CursorBox(UINT32 Cx, UINT32 Cy, UINT32 *Sx, UINT32 *Sy,
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


void DrawCursorAt(UINT32 X, UINT32 Y) {
    int i;

    /* 必须 Raw：客户区 clip 开启时普通 DrawPixel 会让窗外光标消失 */
    for (i = -CURSOR_HALF; i <= CURSOR_HALF; i++) {
        int Px = (int)X + i;
        int Py = (int)Y + i;
        if (Px >= 0 && (UINT32)Px < gScreenW) {
            HalVideoDrawPixelRaw((UINT32)Px, Y, COLOR_WHITE);
        }
        if (Py >= 0 && (UINT32)Py < gScreenH) {
            HalVideoDrawPixelRaw(X, (UINT32)Py, COLOR_WHITE);
        }
    }
    HalVideoDrawPixelRaw(X, Y, COLOR_RED);
}


void CursorRestore(void) {
    UINT32 Dy;
    UINT32 Dx;

    if (!gCursorVisible) {
        return;
    }
    for (Dy = 0; Dy < gSaveH; Dy++) {
        for (Dx = 0; Dx < gSaveW; Dx++) {
            HalVideoDrawPixelRaw(gSaveX + Dx, gSaveY + Dy,
                                 gUnder[Dy * gSaveW + Dx]);
        }
    }
    gCursorVisible = 0;
}


void CursorPaint(void) {
    UINT32 Dy;
    UINT32 Dx;

    /* 已可见时禁止直接再画：否则 gUnder 会采到十字，Restore 后留下印记 */
    if (gCursorVisible) {
        CursorRestore();
    }
    CursorBox(gCursorX, gCursorY, &gSaveX, &gSaveY, &gSaveW, &gSaveH);
    for (Dy = 0; Dy < gSaveH; Dy++) {
        for (Dx = 0; Dx < gSaveW; Dx++) {
            gUnder[Dy * gSaveW + Dx] =
                HalVideoReadPixel(gSaveX + Dx, gSaveY + Dy);
        }
    }
    DrawCursorAt(gCursorX, gCursorY);
    gCursorVisible = 1;
}


void CursorMove(UINT32 X, UINT32 Y) {
    if (X >= gScreenW) {
        X = gScreenW > 0 ? gScreenW - 1 : 0;
    }
    if (Y >= gScreenH) {
        Y = gScreenH > 0 ? gScreenH - 1 : 0;
    }
    if (X == gCursorX && Y == gCursorY) {
        return;
    }

    /* 拖动时只跟踪坐标；若光标仍可见则先擦掉，避免十字残影 */
    if (gDragWin >= 0) {
        if (gCursorVisible) {
            GfxIrqEnter();
            CursorRestore();
            HalVideoPresent();
            GfxIrqLeave();
        }
        gCursorX = X;
        gCursorY = Y;
        return;
    }

    GfxIrqEnter();
    CursorRestore();
    gCursorX = X;
    gCursorY = Y;
    CursorPaint();
    HalVideoPresent();
    GfxIrqLeave();
}


void GuiPointerMove(UINT32 X, UINT32 Y) {
    CursorMove(X, Y);
    if (gDragWin >= 0 && (gCursorBtn & 1)) {
        GuiDragUpdate(X, Y);
    } else if (GuiFocusKind() == GUI_WIN_FILES) {
        /* PR-G11：列表悬停行（不拖动时） */
        FilesUiOnHover(X, Y);
    }
}


/* 帧缓冲绘制前：关中断并擦掉光标（避免 save-under 采到十字像素） */
void GuiFrameBufferBegin(void) {
    GfxIrqEnter();
    CursorRestore();
}


/* 帧缓冲绘制后：重画光标、Present；仅恢复进入 Begin 前已开启的中断 */
void GuiFrameBufferEnd(void) {
    CursorPaint();
    GfxPresent();
    GfxIrqLeave();
}


void GuiCursorPaint(void) {
    GfxIrqEnter();
    CursorRestore();
    CursorPaint();
    GfxPresent();
    GfxIrqLeave();
}


void GuiCursorHide(void) {
    GfxIrqEnter();
    CursorRestore();
    GfxPresent();
    GfxIrqLeave();
}


void GuiCursorShow(void) {
    GfxIrqEnter();
    CursorPaint();
    GfxPresent();
    GfxIrqLeave();
}

