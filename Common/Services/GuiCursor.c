/*
 * GuiCursor.c — 鼠标光标：save-under + 移动/显隐
 *
 * 字形见 GuiCursorShape.c。改大小悬停换形见 GuiResizeCursorKindAt。
 */
#include "GuiPrivate.h"
#include "HalVideo.h"
#include "Hal.h"
#include "UI.h"
#include "FilesUi.h"
#include "SettingsUi.h"
#include "StoreUi.h"
#include "EditUi.h"
#include "Desktop.h"

void CursorBox(UINT32 Cx, UINT32 Cy, UINT32 *Sx, UINT32 *Sy,
                      UINT32 *Sw, UINT32 *Sh) {
    int Half;
    int Thick;
    int L;
    int R;
    int T;
    int B;
    UINT32 ExtPad;
    INT32 X0;
    INT32 Y0;
    INT32 X1;
    INT32 Y1;

    CursorMetrics(&Half, &Thick);
    ExtPad = 0;
    if (HalVideoGetUiScale() != 100u) {
        ExtPad = 2u;
    }
    if (gCursorKind == CURSOR_KIND_ARROW) {
        /*
         * 尖在热点；头约 1.5H、尾再约 0.5H。多留边防拖尾。
         */
        L = Half / 2 + CURSOR_OUTLINE + (int)ExtPad + 4;
        T = Half / 2 + CURSOR_OUTLINE + (int)ExtPad + 4;
        R = Half * 3 + CURSOR_OUTLINE + (int)ExtPad + 6;
        B = Half * 3 + CURSOR_OUTLINE + (int)ExtPad + 6;
    } else {
        /* resize：热点居中 */
        L = R = T = B = Half + Thick + CURSOR_OUTLINE + (int)ExtPad;
        if (gCursorKind == CURSOR_KIND_RESIZE_SE) {
            L = R = T = B = Half + Thick + CURSOR_OUTLINE + 2 + (int)ExtPad;
        }
    }
    X0 = (INT32)Cx - L;
    Y0 = (INT32)Cy - T;
    X1 = (INT32)Cx + R + 1;
    Y1 = (INT32)Cy + B + 1;
    if (X0 < 0) {
        X0 = 0;
    }
    if (Y0 < 0) {
        Y0 = 0;
    }
    if (X1 > (INT32)gScreenWidth) {
        X1 = (INT32)gScreenWidth;
    }
    if (Y1 > (INT32)gScreenHeight) {
        Y1 = (INT32)gScreenHeight;
    }
    *Sx = (UINT32)X0;
    *Sy = (UINT32)Y0;
    *Sw = (UINT32)(X1 - X0);
    *Sh = (UINT32)(Y1 - Y0);
}

void DrawCursorAt(UINT32 X, UINT32 Y) {
    DrawCursorGlyph(X, Y);
}

void CursorRestore(void) {
    if (!gCursorVisible) {
        return;
    }
    if (gSaveWidth > 0 && gSaveHeight > 0 &&
        gSaveWidth <= (UINT32)CURSOR_BOX && gSaveHeight <= (UINT32)CURSOR_BOX) {
        HalVideoCursorOverlayBegin();
        HalVideoWriteRect(gSaveX, gSaveY, gSaveWidth, gSaveHeight, gUnder);
        HalVideoCursorOverlayEnd();
    }
    gCursorVisible = 0;
}

void CursorPaint(void) {
    gCursorKind = GuiResizeCursorKindAt(gCursorX, gCursorY);
    if (gCursorVisible) {
        CursorRestore();
    }
    CursorBox(gCursorX, gCursorY, &gSaveX, &gSaveY, &gSaveWidth, &gSaveHeight);
    if (gSaveWidth == 0 || gSaveHeight == 0 ||
        gSaveWidth > (UINT32)CURSOR_BOX || gSaveHeight > (UINT32)CURSOR_BOX) {
        return;
    }
    HalVideoCursorOverlayBegin();
    HalVideoReadRect(gSaveX, gSaveY, gSaveWidth, gSaveHeight, gUnder);
    DrawCursorGlyph(gCursorX, gCursorY);
    HalVideoCursorOverlayEnd();
    gCursorVisible = 1;
}

void CursorMove(UINT32 X, UINT32 Y) {
    int OldKind;
    int NewKind;

    if (X >= gScreenWidth) {
        X = gScreenWidth > 0 ? gScreenWidth - 1 : 0;
    }
    if (Y >= gScreenHeight) {
        Y = gScreenHeight > 0 ? gScreenHeight - 1 : 0;
    }
    OldKind = gCursorKind;
    NewKind = GuiResizeCursorKindAt(X, Y);
    if (X == gCursorX && Y == gCursorY && OldKind == NewKind) {
        return;
    }

    if (gDragWin >= 0 || DesktopIconDragActive()) {
        if (gCursorVisible) {
            GfxIrqEnter();
            CursorRestore();
            GfxPresent();
            GfxIrqLeave();
        }
        gCursorX = X;
        gCursorY = Y;
        gCursorKind = NewKind;
        return;
    }

    GfxIrqEnter();
    CursorRestore();
    gCursorX = X;
    gCursorY = Y;
    gCursorKind = NewKind;
    CursorPaint();
    GfxPresent();
    GfxIrqLeave();
}

void GuiPointerMove(UINT32 X, UINT32 Y) {
    if (!(gDragWin >= 0 && (gCursorBtn & 1)) &&
        !(gResizeWin >= 0 && (gCursorBtn & 1)) &&
        !(DesktopIconDragActive() && (gCursorBtn & 1))) {
        GuiHoverUpdate(X, Y);
    }
    CursorMove(X, Y);
    if (gResizeWin >= 0 && (gCursorBtn & 1)) {
        GuiResizeUpdate(X, Y);
    } else if (gDragWin >= 0 && (gCursorBtn & 1)) {
        GuiDragUpdate(X, Y);
    } else if (DesktopIconDragActive() && (gCursorBtn & 1)) {
        DesktopIconDragUpdate(X, Y);
    } else if (GuiFocusKind() == GUI_WIN_FILES) {
        FilesUiOnHover(X, Y);
    } else if (GuiFocusKind() == GUI_WIN_SETTINGS) {
        SettingsUiOnPointer(X, Y, gCursorBtn);
    } else if (GuiFocusKind() == GUI_WIN_STORE) {
        if (!StoreUiIsBusy()) {
            StoreUiOnPointer(X, Y, gCursorBtn);
        }
    } else if (GuiFocusKind() == GUI_WIN_EDIT) {
        EditUiOnPointer(X, Y, gCursorBtn);
    }
}

void GuiFrameBufferBegin(void) {
    ComposeBeginEraseCursor();
}

void GuiFrameBufferEnd(void) {
    GfxIrqEnter();
    CursorPaint();
    GfxIrqLeave();
    HalVideoPresentFlush();
    ComposeEnd();
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
