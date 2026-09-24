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
    UINT32 Ext;
    UINT32 Ex;
    UINT32 Ey;

    CursorMetrics(&Half, &Thick);
    Ext = (UINT32)(Half + Thick + CURSOR_OUTLINE);
    if (HalVideoGetUiScale() != 100u) {
        Ext += 2u;
    }
    *Sx = Cx >= Ext ? Cx - Ext : 0;
    *Sy = Cy >= Ext ? Cy - Ext : 0;
    Ex = Cx + Ext + 1;
    Ey = Cy + Ext + 1;
    if (Ex > gScreenWidth) {
        Ex = gScreenWidth;
    }
    if (Ey > gScreenHeight) {
        Ey = gScreenHeight;
    }
    *Sw = Ex - *Sx;
    *Sh = Ey - *Sy;
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
