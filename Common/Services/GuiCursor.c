/*
 * GuiCursor.c — PR-R2：鼠标光标（XOR，无 save-under）
 *
 * 真机跟手：旧 save-under 每移一次 ReadPixel 一整框 + Present，极卡。
 * XOR 再画一次即擦除；与 GuiFrameBufferBegin/End 仍配合（先擦再绘再画上）。
 * 尺寸随 gScreenHeight 相对 1080p 缩放（4K 更大，1080 保持原手感）。
 */
#include "GuiPriv.h"
#include "HalVideo.h"
#include "Hal.h"
#include "UI.h"
#include "FilesUi.h"

#define CURSOR_XOR_MASK 0x00FFFFFFu

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
    Ext = (UINT32)(Half + Thick);
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

/*
 * 粗十字：横条画满，竖条跳过与横条重叠的中心带，避免 XOR 两次抵消。
 */
static void XorCursorAt(UINT32 X, UINT32 Y) {
    int Half;
    int Thick;
    int i;
    int t;

    CursorMetrics(&Half, &Thick);

    for (t = -Thick; t <= Thick; t++) {
        int Py = (int)Y + t;
        if (Py < 0 || (UINT32)Py >= gScreenHeight) {
            continue;
        }
        for (i = -Half; i <= Half; i++) {
            int Px = (int)X + i;
            if (Px >= 0 && (UINT32)Px < gScreenWidth) {
                HalVideoXorPixelRaw((UINT32)Px, (UINT32)Py, CURSOR_XOR_MASK);
            }
        }
    }
    for (t = -Thick; t <= Thick; t++) {
        int Px = (int)X + t;
        if (Px < 0 || (UINT32)Px >= gScreenWidth) {
            continue;
        }
        for (i = -Half; i <= Half; i++) {
            int Py;
            if (i >= -Thick && i <= Thick) {
                continue;
            }
            Py = (int)Y + i;
            if (Py >= 0 && (UINT32)Py < gScreenHeight) {
                HalVideoXorPixelRaw((UINT32)Px, (UINT32)Py, CURSOR_XOR_MASK);
            }
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

/*
 * G7：只锁光标擦/画 + Present；中间绘制开中断。
 * ComposeBusy：嵌套鼠标只改坐标，避免 XOR 光标与正文互踩。
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
