/*
 * FilesUiClick.c — 列表点击与悬停
 * 核心：FilesUi.c
 */
#include "FilesUiPriv.h"

void FilesUiOnClick(UINT32 X, UINT32 Y) {
    UINT32 Cx;
    UINT32 Cy;
    UINT32 Cw;
    UINT32 Ch;
    UINT32 Bg;
    int Row;
    int Idx;
    int NextScroll;
    UINT64 Now;
    UINT64 Dt;
    UINT32 Dx;
    UINT32 Dy;

    if (gMode == FILES_MODE_VIEW || gMode == FILES_MODE_CONFIRM ||
        gMode == FILES_MODE_PROMPT) {
        return;
    }
    if (!GuiFocusClient(&Cx, &Cy, &Cw, &Ch, &Bg)) {
        return;
    }
    if (X < Cx || Y < Cy || X >= Cx + Cw || Y >= Cy + Ch) {
        return;
    }

    /* PR-U2：侧栏书签 */
    if (gSideW > 0 && X < gContentX) {
        Idx = SideHitIndex(X, Y);
        if (Idx >= 0) {
            GotoPath(gPlaces[Idx].Path);
        }
        return;
    }

    /* PR-U3：点在预览区不改选中 */
    if (gPrevW > 0 && X >= gPrevX) {
        return;
    }

    /* PR-G12：滚动条点选 */
    if (gSbVisible &&
        UiScrollBarHit(gSbX, gSbY, gSbW, gSbH, gScroll, gListVisible, gCount,
                       X, Y, &NextScroll)) {
        gScroll = NextScroll;
        if (gSelected < gScroll) {
            gSelected = gScroll;
        }
        if (gSelected >= gScroll + gListVisible) {
            gSelected = gScroll + gListVisible - 1;
        }
        gHoverIdx = -1;
        UpdatePreview();
        PaintList();
        return;
    }

    if (Y < gListTop) {
        return;
    }
    Row = UiListRowFromY(gListTop, gListLineH, gListVisible, Y);
    if (Row < 0) {
        return;
    }
    /* 点在滚动条列上时不当行选 */
    if (gSbVisible && X >= gSbX) {
        return;
    }
    Idx = gScroll + Row;
    if (Idx < 0 || Idx >= gCount) {
        return;
    }

    Now = FilesClock();
    Dt = (Now >= gClickClock) ? (Now - gClickClock) : FILES_DBLCLICK_MAX + 1;
    Dx = (X >= gClickX) ? (X - gClickX) : (gClickX - X);
    Dy = (Y >= gClickY) ? (Y - gClickY) : (gClickY - Y);

    if (Idx == gClickSel &&
        Dt <= FILES_DBLCLICK_MAX &&
        Dx <= FILES_DBLCLICK_SLOP &&
        Dy <= FILES_DBLCLICK_SLOP) {
        gSelected = Idx;
        gClickSel = -1;
        OpenSelected();
        return;
    }

    gSelected = Idx;
    gClickSel = Idx;
    gClickClock = Now;
    gClickX = X;
    gClickY = Y;
    gHoverIdx = Idx;
    UpdatePreview();
    PaintList();
}

void FilesUiOnHover(UINT32 X, UINT32 Y) {
    UINT32 Cx;
    UINT32 Cy;
    UINT32 Cw;
    UINT32 Ch;
    UINT32 Bg;
    int Row;
    int Idx;
    int Prev;

    if (gMode != FILES_MODE_LIST) {
        return;
    }
    if (GuiFocusKind() != GUI_WIN_FILES) {
        return;
    }
    if (!GuiFocusClient(&Cx, &Cy, &Cw, &Ch, &Bg)) {
        return;
    }
    if (X < Cx || Y < Cy || X >= Cx + Cw || Y >= Cy + Ch) {
        if (gHoverIdx >= 0 || gSideHover >= 0) {
            gHoverIdx = -1;
            gSideHover = -1;
            PaintList();
        }
        return;
    }

    /* PR-U2：侧栏悬停 */
    if (gSideW > 0 && X < gContentX) {
        Idx = SideHitIndex(X, Y);
        if (Idx != gSideHover || gHoverIdx >= 0) {
            gSideHover = Idx;
            gHoverIdx = -1;
            PaintList();
        }
        return;
    }

    if (gSideHover >= 0) {
        gSideHover = -1;
    }

    /* PR-U3：预览区无列表悬停 */
    if (gPrevW > 0 && X >= gPrevX) {
        if (gHoverIdx >= 0) {
            gHoverIdx = -1;
            PaintList();
        }
        return;
    }

    Prev = gHoverIdx;
    if (gSbVisible && X >= gSbX) {
        Idx = -1;
    } else if (Y < gListTop || gCount <= 0 || gListLineH == 0) {
        Idx = -1;
    } else {
        Row = UiListRowFromY(gListTop, gListLineH, gListVisible, Y);
        if (Row < 0) {
            Idx = -1;
        } else {
            Idx = gScroll + Row;
            if (Idx < 0 || Idx >= gCount) {
                Idx = -1;
            }
        }
    }
    if (Idx == Prev) {
        return;
    }
    gHoverIdx = Idx;
    PaintList();
}
