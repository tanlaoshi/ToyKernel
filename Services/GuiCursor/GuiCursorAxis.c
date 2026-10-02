/*
 * GuiCursorAxis.c — resize 光标：↔ / ↕ / 对角双向箭头
 */
#include "GuiPrivate.h"
#include "HalVideo.h"
#include "UI.h"

static void PutCursorPx(int Px, int Py, UINT32 Color) {
    if (Px < 0 || Py < 0) {
        return;
    }
    if ((UINT32)Px >= gScreenWidth || (UINT32)Py >= gScreenHeight) {
        return;
    }
    HalVideoDrawPixelRaw((UINT32)Px, (UINT32)Py, Color);
}

static void StrokeSeg(int Cx, int Cy, int X0, int Y0, int X1, int Y1, int Thick) {
    int Dx = X1 - X0;
    int Dy = Y1 - Y0;
    int Steps;
    int i;
    int t;
    int Px;
    int Py;
    int Adx = Dx < 0 ? -Dx : Dx;
    int Ady = Dy < 0 ? -Dy : Dy;

    Steps = Adx > Ady ? Adx : Ady;
    if (Steps < 1) {
        Steps = 1;
    }
    for (i = 0; i <= Steps; i++) {
        Px = X0 + Dx * i / Steps;
        Py = Y0 + Dy * i / Steps;
        for (t = -(Thick + CURSOR_OUTLINE); t <= (Thick + CURSOR_OUTLINE); t++) {
            PutCursorPx(Cx + Px + t, Cy + Py, COLOR_BLACK);
            PutCursorPx(Cx + Px, Cy + Py + t, COLOR_BLACK);
        }
    }
    for (i = 0; i <= Steps; i++) {
        Px = X0 + Dx * i / Steps;
        Py = Y0 + Dy * i / Steps;
        for (t = -Thick; t <= Thick; t++) {
            PutCursorPx(Cx + Px + t, Cy + Py, COLOR_WHITE);
            PutCursorPx(Cx + Px, Cy + Py + t, COLOR_WHITE);
        }
    }
}

static void AxisArrowHead(int Cx, int Cy, int TipX, int TipY, int Dx, int Dy,
                          int Half, int Thick) {
    int Wing = Half / 2;
    int i;
    int Wx;
    int Wy;
    int Bx;
    int By;

    if (Wing < 2) {
        Wing = 2;
    }
    Wx = -Dy;
    Wy = Dx;
    for (i = 0; i <= Wing; i++) {
        Bx = TipX - Dx * (i + 1) + Wx * (Wing - i);
        By = TipY - Dy * (i + 1) + Wy * (Wing - i);
        StrokeSeg(Cx, Cy, TipX, TipY, Bx, By, Thick);
        Bx = TipX - Dx * (i + 1) - Wx * (Wing - i);
        By = TipY - Dy * (i + 1) - Wy * (Wing - i);
        StrokeSeg(Cx, Cy, TipX, TipY, Bx, By, Thick);
    }
}

/* Axis：0=↔，1=↕，2=对角 SE↔NW */
void DrawCursorAxis(UINT32 X, UINT32 Y, int Axis) {
    int Half;
    int Thick;
    int Ax;
    int Ay;
    int Cx = (int)X;
    int Cy = (int)Y;

    CursorMetrics(&Half, &Thick);
    if (Axis == 0) {
        Ax = Half;
        Ay = 0;
    } else if (Axis == 1) {
        Ax = 0;
        Ay = Half;
    } else {
        Ax = Half;
        Ay = Half;
    }
    StrokeSeg(Cx, Cy, -Ax, -Ay, Ax, Ay, Thick);
    if (Axis == 0) {
        AxisArrowHead(Cx, Cy, Ax, Ay, 1, 0, Half, Thick);
        AxisArrowHead(Cx, Cy, -Ax, -Ay, -1, 0, Half, Thick);
    } else if (Axis == 1) {
        AxisArrowHead(Cx, Cy, Ax, Ay, 0, 1, Half, Thick);
        AxisArrowHead(Cx, Cy, -Ax, -Ay, 0, -1, Half, Thick);
    } else {
        AxisArrowHead(Cx, Cy, Ax, Ay, 1, 1, Half, Thick);
        AxisArrowHead(Cx, Cy, -Ax, -Ay, -1, -1, Half, Thick);
    }
}
