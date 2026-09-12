/*
 * GuiCursor.c — PR-R2：鼠标光标（save-under 实心十字）
 *
 * XOR(0xFFFFFF) 在未聚焦标题栏 COLOR_GRAY(0x808080) 上几乎不可见
 * （^ 后 ≈ 0x7F7F7F），看起来像「被标题栏盖住」；聚焦蓝栏则成亮黄。
 * 改为读下底层 → 画黑边白芯 → 擦除写回；脏区走 CursorOverlay，
 * 不与 Shell 内容并 AABB（保 4K Present 热修）。
 */
#include "GuiPriv.h"
#include "HalVideo.h"
#include "Hal.h"
#include "UI.h"
#include "FilesUi.h"

/* 臂长：1080→6，2160→12；线半宽：1080→0（1px），2160→1（3px） */
static void CursorMetrics(int *Half, int *Thick) {
    UINT32 H;
    UINT32 HalfU;
    int T;

    H = gScreenHeight != 0 ? gScreenHeight : CURSOR_REF_H;
    HalfU = (CURSOR_HALF_BASE * H + CURSOR_REF_H / 2u) / CURSOR_REF_H;
    if (HalfU < 4u) {
        HalfU = 4u;
    }
    if (HalfU > (UINT32)CURSOR_HALF_MAX) {
        HalfU = (UINT32)CURSOR_HALF_MAX;
    }
    *Half = (int)HalfU;
    T = (int)HalfU / CURSOR_HALF_BASE;
    if (T > 0) {
        T--;
    }
    if (T > CURSOR_THICK_MAX) {
        T = CURSOR_THICK_MAX;
    }
    *Thick = T;
}

void CursorBox(UINT32 Cx, UINT32 Cy, UINT32 *Sx, UINT32 *Sy,
                      UINT32 *Sw, UINT32 *Sh) {
    int Half;
    int Thick;
    UINT32 Ext;
    UINT32 Ex;
    UINT32 Ey;

    CursorMetrics(&Half, &Thick);
    /* 须含黑描边外扩，否则描边像素不在 gUnder 内 → 移动留黑尾巴 */
    Ext = (UINT32)(Half + Thick + CURSOR_OUTLINE);
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

static void PutCursorPx(int Px, int Py, UINT32 Color) {
    if (Px < 0 || Py < 0) {
        return;
    }
    if ((UINT32)Px >= gScreenWidth || (UINT32)Py >= gScreenHeight) {
        return;
    }
    HalVideoDrawPixelRaw((UINT32)Px, (UINT32)Py, Color);
}

/*
 * 粗十字：先黑描边再白芯，任意底色（含未聚焦灰标题栏）都压在最上层。
 */
static void DrawCursorGlyph(UINT32 X, UINT32 Y) {
    int Half;
    int Thick;
    int i;
    int t;

    CursorMetrics(&Half, &Thick);

    /* 黑描边（外扩 CURSOR_OUTLINE） */
    for (t = -(Thick + CURSOR_OUTLINE); t <= (Thick + CURSOR_OUTLINE); t++) {
        for (i = -(Half + CURSOR_OUTLINE); i <= (Half + CURSOR_OUTLINE); i++) {
            PutCursorPx((int)X + i, (int)Y + t, COLOR_BLACK);
        }
    }
    for (t = -(Thick + CURSOR_OUTLINE); t <= (Thick + CURSOR_OUTLINE); t++) {
        for (i = -(Half + CURSOR_OUTLINE); i <= (Half + CURSOR_OUTLINE); i++) {
            if (i >= -(Thick + CURSOR_OUTLINE) && i <= (Thick + CURSOR_OUTLINE)) {
                continue;
            }
            PutCursorPx((int)X + t, (int)Y + i, COLOR_BLACK);
        }
    }
    /* 白芯 */
    for (t = -Thick; t <= Thick; t++) {
        for (i = -Half; i <= Half; i++) {
            PutCursorPx((int)X + i, (int)Y + t, COLOR_WHITE);
        }
    }
    for (t = -Thick; t <= Thick; t++) {
        for (i = -Half; i <= Half; i++) {
            if (i >= -Thick && i <= Thick) {
                continue;
            }
            PutCursorPx((int)X + t, (int)Y + i, COLOR_WHITE);
        }
    }
}

void DrawCursorAt(UINT32 X, UINT32 Y) {
    DrawCursorGlyph(X, Y);
}

void CursorRestore(void) {
    if (!gCursorVisible) {
        return;
    }
    if (gSaveW > 0 && gSaveH > 0 &&
        gSaveW <= (UINT32)CURSOR_BOX && gSaveH <= (UINT32)CURSOR_BOX) {
        HalVideoCursorOverlayBegin();
        HalVideoWriteRect(gSaveX, gSaveY, gSaveW, gSaveH, gUnder);
        HalVideoCursorOverlayEnd();
    }
    gCursorVisible = 0;
}

void CursorPaint(void) {
    if (gCursorVisible) {
        CursorRestore();
    }
    CursorBox(gCursorX, gCursorY, &gSaveX, &gSaveY, &gSaveW, &gSaveH);
    if (gSaveW == 0 || gSaveH == 0 ||
        gSaveW > (UINT32)CURSOR_BOX || gSaveH > (UINT32)CURSOR_BOX) {
        return;
    }
    HalVideoCursorOverlayBegin();
    HalVideoReadRect(gSaveX, gSaveY, gSaveW, gSaveH, gUnder);
    DrawCursorGlyph(gCursorX, gCursorY);
    HalVideoCursorOverlayEnd();
    gCursorVisible = 1;
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

/*
 * G7：只锁光标擦/画 + Present；中间绘制开中断。
 * ComposeBusy：嵌套鼠标只改坐标，避免光标与正文互踩。
 * 4K：旧路径整段 cli + DirtyUnion(Shell∪远处光标)→近全屏 Present 饿死 USB。
 */
void GuiFrameBufferBegin(void) {
    ComposeBegin();
    GfxIrqEnter();
    CursorRestore();
    GfxIrqLeave();
}

void GuiFrameBufferEnd(void) {
    GfxIrqEnter();
    CursorPaint();
    GfxPresent();
    GfxIrqLeave();
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
