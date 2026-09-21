/*
 * GuiCursorShape.c — 光标字形：箭头 / 右 / 底 / 对角 resize
 */
#include "GuiPrivate.h"
#include "HalVideo.h"
#include "UI.h"

void CursorMetrics(int *Half, int *Thick) {
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

static void PutCursorPx(int Px, int Py, UINT32 Color) {
    if (Px < 0 || Py < 0) {
        return;
    }
    if ((UINT32)Px >= gScreenWidth || (UINT32)Py >= gScreenHeight) {
        return;
    }
    HalVideoDrawPixelRaw((UINT32)Px, (UINT32)Py, Color);
}

static void DrawCursorArrow(UINT32 X, UINT32 Y) {
    int Half;
    int Thick;
    int i;
    int t;

    CursorMetrics(&Half, &Thick);
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

/* Axis：0=水平 E，1=竖直 S，2=对角 SE */
static void DrawCursorAxis(UINT32 X, UINT32 Y, int Axis) {
    int Half;
    int Thick;
    int i;
    int t;
    int Ox;
    int Oy;

    CursorMetrics(&Half, &Thick);
    for (i = -Half; i <= Half; i++) {
        for (t = -(Thick + CURSOR_OUTLINE); t <= (Thick + CURSOR_OUTLINE); t++) {
            if (Axis == 0) {
                Ox = i;
                Oy = t;
            } else if (Axis == 1) {
                Ox = t;
                Oy = i;
            } else {
                Ox = i;
                Oy = i + t;
            }
            PutCursorPx((int)X + Ox, (int)Y + Oy, COLOR_BLACK);
            if (Axis == 2) {
                PutCursorPx((int)X + Ox, (int)Y + i - t, COLOR_BLACK);
            }
        }
        for (t = -Thick; t <= Thick; t++) {
            if (Axis == 0) {
                Ox = i;
                Oy = t;
            } else if (Axis == 1) {
                Ox = t;
                Oy = i;
            } else {
                Ox = i;
                Oy = i + t;
            }
            PutCursorPx((int)X + Ox, (int)Y + Oy, COLOR_WHITE);
            if (Axis == 2) {
                PutCursorPx((int)X + Ox, (int)Y + i - t, COLOR_WHITE);
            }
        }
    }
    for (t = -Half / 2; t <= Half / 2; t++) {
        if (Axis == 0) {
            PutCursorPx((int)X - Half, (int)Y + t, COLOR_WHITE);
            PutCursorPx((int)X + Half, (int)Y + t, COLOR_WHITE);
            PutCursorPx((int)X - Half - 1, (int)Y + t, COLOR_BLACK);
            PutCursorPx((int)X + Half + 1, (int)Y + t, COLOR_BLACK);
        } else if (Axis == 1) {
            PutCursorPx((int)X + t, (int)Y - Half, COLOR_WHITE);
            PutCursorPx((int)X + t, (int)Y + Half, COLOR_WHITE);
            PutCursorPx((int)X + t, (int)Y - Half - 1, COLOR_BLACK);
            PutCursorPx((int)X + t, (int)Y + Half + 1, COLOR_BLACK);
        } else {
            PutCursorPx((int)X - Half + t, (int)Y - Half, COLOR_WHITE);
            PutCursorPx((int)X + Half, (int)Y + Half - t, COLOR_WHITE);
            PutCursorPx((int)X - Half, (int)Y - Half + t, COLOR_WHITE);
            PutCursorPx((int)X + Half - t, (int)Y + Half, COLOR_WHITE);
        }
    }
}

void DrawCursorGlyph(UINT32 X, UINT32 Y) {
    if (gCursorKind == CURSOR_KIND_RESIZE_E) {
        DrawCursorAxis(X, Y, 0);
    } else if (gCursorKind == CURSOR_KIND_RESIZE_S) {
        DrawCursorAxis(X, Y, 1);
    } else if (gCursorKind == CURSOR_KIND_RESIZE_SE) {
        DrawCursorAxis(X, Y, 2);
    } else {
        DrawCursorArrow(X, Y);
    }
}
