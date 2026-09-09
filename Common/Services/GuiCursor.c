/*
 * GuiCursor.c — PR-R2：鼠标光标（XOR，无 save-under）
 *
 * 真机跟手：旧 save-under 每移一次 ReadPixel 一整框 + Present，极卡。
 * XOR 再画一次即擦除；与 GuiFrameBufferBegin/End 仍配合（先擦再绘再画上）。
 */
#include "GuiPriv.h"
#include "HalVideo.h"
#include "Hal.h"
#include "UI.h"
#include "FilesUi.h"

#define CURSOR_XOR_MASK 0x00FFFFFFu

void CursorBox(UINT32 Cx, UINT32 Cy, UINT32 *Sx, UINT32 *Sy,
                      UINT32 *Sw, UINT32 *Sh) {
    *Sx = Cx >= CURSOR_HALF ? Cx - CURSOR_HALF : 0;
    *Sy = Cy >= CURSOR_HALF ? Cy - CURSOR_HALF : 0;
    UINT32 Ex = Cx + CURSOR_HALF + 1;
    UINT32 Ey = Cy + CURSOR_HALF + 1;
    if (Ex > gScreenWidth) {
        Ex = gScreenWidth;
    }
    if (Ey > gScreenHeight) {
        Ey = gScreenHeight;
    }
    *Sw = Ex - *Sx;
    *Sh = Ey - *Sy;
}

/* 横条含中心，竖条跳过中心，避免中心被异或两次变回原色 */
static void XorCursorAt(UINT32 X, UINT32 Y) {
    int i;

    for (i = -CURSOR_HALF; i <= CURSOR_HALF; i++) {
        int Px = (int)X + i;
        if (Px >= 0 && (UINT32)Px < gScreenWidth) {
            HalVideoXorPixelRaw((UINT32)Px, Y, CURSOR_XOR_MASK);
        }
    }
    for (i = -CURSOR_HALF; i <= CURSOR_HALF; i++) {
        int Py;
        if (i == 0) {
            continue;
        }
        Py = (int)Y + i;
        if (Py >= 0 && (UINT32)Py < gScreenHeight) {
            HalVideoXorPixelRaw(X, (UINT32)Py, CURSOR_XOR_MASK);
        }
    }
}

void DrawCursorAt(UINT32 X, UINT32 Y) {
    XorCursorAt(X, Y);
}

void CursorRestore(void) {
    if (!gCursorVisible) {
        return;
    }
    XorCursorAt(gCursorX, gCursorY);
    gCursorVisible = 0;
}

void CursorPaint(void) {
    if (gCursorVisible) {
        CursorRestore();
    }
    XorCursorAt(gCursorX, gCursorY);
    gCursorVisible = 1;
    /* gSave* 仍更新，供调试/兼容；XOR 路径不再读 gUnder */
    CursorBox(gCursorX, gCursorY, &gSaveX, &gSaveY, &gSaveW, &gSaveH);
}

void CursorMove(UINT32 X, UINT32 Y) {
    if (X >= gScreenWidth) {
        X = gScreenWidth > 0 ? gScreenWidth - 1 : 0;
    }
    if (Y >= gScreenHeight) {
        Y = gScreenHeight > 0 ? gScreenHeight - 1 : 0;
    }
    if (X == gCursorX && Y == gCursorY) {
        return;
    }

    if (gDragWin >= 0) {
        if (gCursorVisible) {
            GfxIrqEnter();
            CursorRestore();
            GfxPresent();
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
    GfxPresent();
    GfxIrqLeave();
}

void GuiPointerMove(UINT32 X, UINT32 Y) {
    CursorMove(X, Y);
    if (gDragWin >= 0 && (gCursorBtn & 1)) {
        GuiDragUpdate(X, Y);
    } else if (GuiFocusKind() == GUI_WIN_FILES) {
        FilesUiOnHover(X, Y);
    }
}

void GuiFrameBufferBegin(void) {
    GfxIrqEnter();
    CursorRestore();
}

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
