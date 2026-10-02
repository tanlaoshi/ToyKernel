/*
 * UI.c — PR-S3-ui-1：几何核心（线/矩形/圆/三角 + 圆角）
 *
 * 按钮/进度见 UIDraw.c；命中/滚动见 UILayout.c。
 */
#include "UI.h"
#include "HalVideo.h"

/* 整数绝对值 */
static int Abs(int x) {
    return (x < 0) ? -x : x;
}

/* 整数平方根（牛顿迭代） */
static int ISqrt(int n) {
    if (n <= 0) return 0;
    int x = n;
    int y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

/* 绘制直线（整数 Bresenham；无浮点，便于 Arm -mgeneral-regs-only） */
void UiDrawLine(UINT32 X1, UINT32 Y1, UINT32 X2, UINT32 Y2, UINT32 Color) {
    int X0 = (int)X1;
    int Y0 = (int)Y1;
    int Xn = (int)X2;
    int Yn = (int)Y2;
    int Dx = Abs(Xn - X0);
    int Sx = X0 < Xn ? 1 : -1;
    int Dy = -Abs(Yn - Y0);
    int Sy = Y0 < Yn ? 1 : -1;
    int Err = Dx + Dy;

    for (;;) {
        HalVideoDrawPixel((UINT32)X0, (UINT32)Y0, Color);
        if (X0 == Xn && Y0 == Yn) {
            break;
        }
        {
            int E2 = 2 * Err;
            if (E2 >= Dy) {
                Err += Dy;
                X0 += Sx;
            }
            if (E2 <= Dx) {
                Err += Dx;
                Y0 += Sy;
            }
        }
    }
}

/* 绘制空心矩形边框 */
void UiDrawRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color) {
    if (Width < 2 || Height < 2) return;

    UiDrawLine(X, Y, X + Width - 1, Y, Color);
    UiDrawLine(X, Y + Height - 1, X + Width - 1, Y + Height - 1, Color);
    UiDrawLine(X, Y, X, Y + Height - 1, Color);
    UiDrawLine(X + Width - 1, Y, X + Width - 1, Y + Height - 1, Color);
}

/* 填充实心矩形 */
void UiFillRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color) {
    HalVideoFillRect(X, Y, Width, Height, Color);
}

UINT32 UiBlendRgb(UINT32 Dst, UINT32 Src, UINT8 Alpha) {
    return HalVideoBlendRgb(Dst, Src, Alpha);
}

void UiFillRectangleAlpha(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height,
                          UINT32 Color, UINT8 Alpha) {
    HalVideoBlendFillRect(X, Y, Width, Height, Color, Alpha);
}

/* 绘制圆角空心矩形 */
void UiDrawRoundRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Radius, UINT32 Color) {
    if (Radius > Width / 2) Radius = Width / 2;
    if (Radius > Height / 2) Radius = Height / 2;
    if (Radius == 0) {
        UiDrawRectangle(X, Y, Width, Height, Color);
        return;
    }

    UiDrawLine(X + Radius, Y, X + Width - Radius, Y, Color);
    UiDrawLine(X + Radius, Y + Height, X + Width - Radius, Y + Height, Color);
    UiDrawLine(X, Y + Radius, X, Y + Height - Radius, Color);
    UiDrawLine(X + Width, Y + Radius, X + Width, Y + Height - Radius, Color);

    {
        int r = (int)Radius;
        int Cx1 = (int)(X + Radius);
        int Cy1 = (int)(Y + Radius);
        int Cx2 = (int)(X + Width - Radius);
        int Cy2 = (int)(Y + Radius);
        int Cx3 = (int)(X + Radius);
        int Cy3 = (int)(Y + Height - Radius);
        int Cx4 = (int)(X + Width - Radius);
        int Cy4 = (int)(Y + Height - Radius);
        int x = 0;
        int y = r;
        int d = 3 - 2 * r;

        while (x <= y) {
            HalVideoDrawPixel(Cx1 - x, Cy1 - y, Color);
            HalVideoDrawPixel(Cx1 - y, Cy1 - x, Color);
            HalVideoDrawPixel(Cx2 + x, Cy2 - y, Color);
            HalVideoDrawPixel(Cx2 + y, Cy2 - x, Color);
            HalVideoDrawPixel(Cx3 - x, Cy3 + y, Color);
            HalVideoDrawPixel(Cx3 - y, Cy3 + x, Color);
            HalVideoDrawPixel(Cx4 + x, Cy4 + y, Color);
            HalVideoDrawPixel(Cx4 + y, Cy4 + x, Color);

            if (d < 0) {
                d += 4 * x + 6;
            } else {
                d += 4 * (x - y) + 10;
                y--;
            }
            x++;
        }
    }
}

/* 填充实心圆角矩形 */
void UiFillRoundRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Radius, UINT32 Color) {
    if (Radius > Width / 2) Radius = Width / 2;
    if (Radius > Height / 2) Radius = Height / 2;
    if (Radius == 0) {
        UiFillRectangle(X, Y, Width, Height, Color);
        return;
    }

    for (UINT32 Row = 0; Row < Height; Row++) {
        for (UINT32 Col = 0; Col < Width; Col++) {
            UINT32 GlobalX = X + Col;
            UINT32 GlobalY = Y + Row;
            int DeltaLeft = (int)Col - (int)Radius;
            int DeltaRight = (int)(Width - 1 - Col) - (int)Radius;
            int DeltaTop = (int)Row - (int)Radius;
            int DeltaBottom = (int)(Height - 1 - Row) - (int)Radius;
            int Inside = 1;

            if (Col < Radius && Row < Radius) {
                if (DeltaLeft * DeltaLeft + DeltaTop * DeltaTop > (int)(Radius * Radius)) {
                    Inside = 0;
                }
            }
            if (Col > Width - 1 - Radius && Row < Radius) {
                if (DeltaRight * DeltaRight + DeltaTop * DeltaTop > (int)(Radius * Radius)) {
                    Inside = 0;
                }
            }
            if (Col < Radius && Row > Height - 1 - Radius) {
                if (DeltaLeft * DeltaLeft + DeltaBottom * DeltaBottom > (int)(Radius * Radius)) {
                    Inside = 0;
                }
            }
            if (Col > Width - 1 - Radius && Row > Height - 1 - Radius) {
                if (DeltaRight * DeltaRight + DeltaBottom * DeltaBottom > (int)(Radius * Radius)) {
                    Inside = 0;
                }
            }

            if (Inside) {
                HalVideoDrawPixel(GlobalX, GlobalY, Color);
            }
        }
    }
}

/* 绘制空心圆（中点圆算法） */
void UiDrawCircle(UINT32 CenterX, UINT32 CenterY, UINT32 Radius, UINT32 Color) {
    if (Radius == 0) {
        HalVideoDrawPixel(CenterX, CenterY, Color);
        return;
    }

    {
        int x = 0;
        int y = (int)Radius;
        int d = 3 - 2 * (int)Radius;

        while (y >= x) {
            HalVideoDrawPixel(CenterX + x, CenterY + y, Color);
            HalVideoDrawPixel(CenterX - x, CenterY + y, Color);
            HalVideoDrawPixel(CenterX + x, CenterY - y, Color);
            HalVideoDrawPixel(CenterX - x, CenterY - y, Color);
            HalVideoDrawPixel(CenterX + y, CenterY + x, Color);
            HalVideoDrawPixel(CenterX - y, CenterY + x, Color);
            HalVideoDrawPixel(CenterX + y, CenterY - x, Color);
            HalVideoDrawPixel(CenterX - y, CenterY - x, Color);

            if (d < 0) {
                d += 4 * x + 6;
            } else {
                d += 4 * (x - y) + 10;
                y--;
            }
            x++;
        }
    }
}

/* 填充实心圆 */
void UiFillCircle(UINT32 CenterX, UINT32 CenterY, UINT32 Radius, UINT32 Color) {
    if (Radius == 0) {
        HalVideoDrawPixel(CenterX, CenterY, Color);
        return;
    }

    for (int y = -(int)Radius; y <= (int)Radius; y++) {
        int x = ISqrt((int)(Radius * Radius) - y * y);
        for (int i = -x; i <= x; i++) {
            HalVideoDrawPixel(CenterX + i, CenterY + y, Color);
        }
    }
}

/* 绘制三角形边框（三条边） */
void UiDrawTriangle(UINT32 X1, UINT32 Y1, UINT32 X2, UINT32 Y2, UINT32 X3, UINT32 Y3, UINT32 Color) {
    UiDrawLine(X1, Y1, X2, Y2, Color);
    UiDrawLine(X2, Y2, X3, Y3, Color);
    UiDrawLine(X3, Y3, X1, Y1, Color);
}

/* 填充实心三角形（扫描线） */
void UiFillTriangle(UINT32 X1, UINT32 Y1, UINT32 X2, UINT32 Y2, UINT32 X3, UINT32 Y3, UINT32 Color) {
    UiDrawTriangle(X1, Y1, X2, Y2, X3, Y3, Color);
}
