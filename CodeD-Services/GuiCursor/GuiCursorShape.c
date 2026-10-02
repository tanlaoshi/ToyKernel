/*
 * GuiCursorShape.c — 光标字形：指针箭头 / ↔ / ↕ / 对角 resize
 *
 * 热点：箭头 = 尖端；resize = 字形中心。
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

/*
 * 指针：等腰三角（尖=热点）+ 沿对称轴伸出的尾巴。
 * 轴 (1,1) 正右下 45°，底边用垂直向量 (-1,1) 对称展开，避免 (1,2) 那般左下歪。
 */
static int Orient2(int Ax, int Ay, int Bx, int By, int Cx, int Cy) {
    return (Bx - Ax) * (Cy - Ay) - (By - Ay) * (Cx - Ax);
}

static int InTriangle(int Px, int Py, int X0, int Y0, int X1, int Y1, int X2,
                      int Y2) {
    int O1 = Orient2(X0, Y0, X1, Y1, Px, Py);
    int O2 = Orient2(X1, Y1, X2, Y2, Px, Py);
    int O3 = Orient2(X2, Y2, X0, Y0, Px, Py);
    int Neg = (O1 < 0) || (O2 < 0) || (O3 < 0);
    int Pos = (O1 > 0) || (O2 > 0) || (O3 > 0);

    return !(Neg && Pos);
}

/* 点到线段距离² ≤ R² */
static int NearSeg(int Px, int Py, int X0, int Y0, int X1, int Y1, int R) {
    int Dx = X1 - X0;
    int Dy = Y1 - Y0;
    int L2 = Dx * Dx + Dy * Dy;
    int T;
    int Qx;
    int Qy;
    int Ex;
    int Ey;

    if (L2 <= 0) {
        Ex = Px - X0;
        Ey = Py - Y0;
        return Ex * Ex + Ey * Ey <= R * R;
    }
    T = ((Px - X0) * Dx + (Py - Y0) * Dy);
    if (T < 0) {
        T = 0;
    }
    if (T > L2) {
        T = L2;
    }
    Qx = X0 + (Dx * T) / L2;
    Qy = Y0 + (Dy * T) / L2;
    Ex = Px - Qx;
    Ey = Py - Qy;
    return Ex * Ex + Ey * Ey <= R * R;
}

static void ArrowGeom(int *TipX, int *TipY, int *Lx, int *Ly, int *Rx, int *Ry,
                      int *Bx, int *By, int *Ex, int *Ey, int *StemR) {
    int Half;
    int Thick;
    int Head;
    int Stem;
    int Wing;

    CursorMetrics(&Half, &Thick);
    /* 三角高度再矮一点；翼仍窄保细尖；尾短细 */
    Head = Half;
    if (Head < 6) {
        Head = 6;
    }
    Stem = Half / 2; /* 再短 2px（原 Half/2+2） */
    if (Stem < 2) {
        Stem = 2;
    }
    Wing = Half / 2;
    if (Wing < 2) {
        Wing = 2;
    }
    *TipX = 0;
    *TipY = 0;
    *Bx = Head;
    *By = Head;
    *Lx = *Bx - Wing;
    *Ly = *By + Wing;
    *Rx = *Bx + Wing;
    *Ry = *By - Wing;
    *Ex = *Bx + Stem;
    *Ey = *By + Stem;
    *StemR = (Thick > 0) ? Thick : 1;
}

static int ArrowSolid(int Ox, int Oy) {
    int TipX;
    int TipY;
    int Lx;
    int Ly;
    int Rx;
    int Ry;
    int Bx;
    int By;
    int Ex;
    int Ey;
    int StemR;

    ArrowGeom(&TipX, &TipY, &Lx, &Ly, &Rx, &Ry, &Bx, &By, &Ex, &Ey, &StemR);
    if (InTriangle(Ox, Oy, TipX, TipY, Lx, Ly, Rx, Ry)) {
        return 1;
    }
    /* 尾巴只接在底边中点之外，勿画穿三角（穿心会又粗又丑） */
    if (NearSeg(Ox, Oy, Bx, By, Ex, Ey, StemR)) {
        return 1;
    }
    return 0;
}

static void DrawCursorArrow(UINT32 X, UINT32 Y) {
    int TipX;
    int TipY;
    int Lx;
    int Ly;
    int Rx;
    int Ry;
    int Bx;
    int By;
    int Ex;
    int Ey;
    int StemR;
    int MinX;
    int MaxX;
    int MinY;
    int MaxY;
    int Ox;
    int Oy;

    ArrowGeom(&TipX, &TipY, &Lx, &Ly, &Rx, &Ry, &Bx, &By, &Ex, &Ey, &StemR);
    MinX = TipX;
    MaxX = TipX;
    MinY = TipY;
    MaxY = TipY;
    if (Lx < MinX) {
        MinX = Lx;
    }
    if (Rx < MinX) {
        MinX = Rx;
    }
    if (Ex < MinX) {
        MinX = Ex;
    }
    if (Lx > MaxX) {
        MaxX = Lx;
    }
    if (Rx > MaxX) {
        MaxX = Rx;
    }
    if (Ex > MaxX) {
        MaxX = Ex;
    }
    if (Ly < MinY) {
        MinY = Ly;
    }
    if (Ry < MinY) {
        MinY = Ry;
    }
    if (Ey < MinY) {
        MinY = Ey;
    }
    if (Ly > MaxY) {
        MaxY = Ly;
    }
    if (Ry > MaxY) {
        MaxY = Ry;
    }
    if (Ey > MaxY) {
        MaxY = Ey;
    }
    MinX -= StemR + CURSOR_OUTLINE + 1;
    MinY -= StemR + CURSOR_OUTLINE + 1;
    MaxX += StemR + CURSOR_OUTLINE + 1;
    MaxY += StemR + CURSOR_OUTLINE + 1;

    for (Oy = MinY; Oy <= MaxY; Oy++) {
        for (Ox = MinX; Ox <= MaxX; Ox++) {
            if (ArrowSolid(Ox, Oy)) {
                continue;
            }
            if (ArrowSolid(Ox - 1, Oy) || ArrowSolid(Ox + 1, Oy) ||
                ArrowSolid(Ox, Oy - 1) || ArrowSolid(Ox, Oy + 1)) {
                PutCursorPx((int)X + Ox, (int)Y + Oy, COLOR_BLACK);
            }
        }
    }
    for (Oy = MinY; Oy <= MaxY; Oy++) {
        for (Ox = MinX; Ox <= MaxX; Ox++) {
            if (ArrowSolid(Ox, Oy)) {
                PutCursorPx((int)X + Ox, (int)Y + Oy, COLOR_WHITE);
            }
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
