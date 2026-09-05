/*
 * UI.c — 帧缓冲图形与 UI 控件
 *
 * 提供直线、矩形、圆、三角形及按钮、进度条等绘制函数，供演示或扩展界面使用。
 */
#include "UI.h"
#include "HalVideo.h"
#include "Font.h"

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

/* 绘制圆角空心矩形 */
void UiDrawRoundRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Radius, UINT32 Color) {
    if (Radius > Width / 2) Radius = Width / 2;
    if (Radius > Height / 2) Radius = Height / 2;
    if (Radius == 0) {
        UiDrawRectangle(X, Y, Width, Height, Color);
        return;
    }
    
    // 画四条直线边
    UiDrawLine(X + Radius, Y, X + Width - Radius, Y, Color);
    UiDrawLine(X + Radius, Y + Height, X + Width - Radius, Y + Height, Color);
    UiDrawLine(X, Y + Radius, X, Y + Height - Radius, Color);
    UiDrawLine(X + Width, Y + Radius, X + Width, Y + Height - Radius, Color);
    
    // 使用 Bresenham 画四个 1/4 圆弧
    int r = Radius;
    int cx1 = X + Radius;
    int cy1 = Y + Radius;
    int cx2 = X + Width - Radius;
    int cy2 = Y + Radius;
    int cx3 = X + Radius;
    int cy3 = Y + Height - Radius;
    int cx4 = X + Width - Radius;
    int cy4 = Y + Height - Radius;
    
    int x = 0;
    int y = r;
    int d = 3 - 2 * r;
    
    while (x <= y) {
        // 左上角
        HalVideoDrawPixel(cx1 - x, cy1 - y, Color);
        HalVideoDrawPixel(cx1 - y, cy1 - x, Color);
        // 右上角
        HalVideoDrawPixel(cx2 + x, cy2 - y, Color);
        HalVideoDrawPixel(cx2 + y, cy2 - x, Color);
        // 左下角
        HalVideoDrawPixel(cx3 - x, cy3 + y, Color);
        HalVideoDrawPixel(cx3 - y, cy3 + x, Color);
        // 右下角
        HalVideoDrawPixel(cx4 + x, cy4 + y, Color);
        HalVideoDrawPixel(cx4 + y, cy4 + x, Color);
        
        if (d < 0) {
            d += 4 * x + 6;
        } else {
            d += 4 * (x - y) + 10;
            y--;
        }
        x++;
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
    
    // 方法：逐行扫描，判断每个像素是否在圆角矩形内
    for (UINT32 row = 0; row < Height; row++) {
        for (UINT32 col = 0; col < Width; col++) {
            UINT32 globalX = X + col;
            UINT32 globalY = Y + row;
            
            // 判断是否在四个圆角区域之外
            int dx_left = col - Radius;
            int dx_right = (Width - 1 - col) - Radius;
            int dy_top = row - Radius;
            int dy_bottom = (Height - 1 - row) - Radius;
            
            int inside = 1;
            
            // 左上角
            if (col < Radius && row < Radius) {
                if (dx_left * dx_left + dy_top * dy_top > (int)(Radius * Radius)) {
                    inside = 0;
                }
            }
            // 右上角
            if (col > Width - 1 - Radius && row < Radius) {
                if (dx_right * dx_right + dy_top * dy_top > (int)(Radius * Radius)) {
                    inside = 0;
                }
            }
            // 左下角
            if (col < Radius && row > Height - 1 - Radius) {
                if (dx_left * dx_left + dy_bottom * dy_bottom > (int)(Radius * Radius)) {
                    inside = 0;
                }
            }
            // 右下角
            if (col > Width - 1 - Radius && row > Height - 1 - Radius) {
                if (dx_right * dx_right + dy_bottom * dy_bottom > (int)(Radius * Radius)) {
                    inside = 0;
                }
            }
            
            if (inside) {
                HalVideoDrawPixel(globalX, globalY, Color);
            }
        }
    }
}

// ============================================================
//  圆形
// ============================================================

/* 绘制空心圆（中点圆算法） */
void UiDrawCircle(UINT32 CenterX, UINT32 CenterY, UINT32 Radius, UINT32 Color) {
    if (Radius == 0) {
        HalVideoDrawPixel(CenterX, CenterY, Color);
        return;
    }
    
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

/* 填充实心圆 */
void UiFillCircle(UINT32 CenterX, UINT32 CenterY, UINT32 Radius, UINT32 Color) {
    if (Radius == 0) {
        HalVideoDrawPixel(CenterX, CenterY, Color);
        return;
    }
    
    for (int y = -(int)Radius; y <= (int)Radius; y++) {
        int x = ISqrt(Radius * Radius - y * y);
        for (int i = -x; i <= x; i++) {
            HalVideoDrawPixel(CenterX + i, CenterY + y, Color);
        }
    }
}

// ============================================================
//  三角形（简化版）
// ============================================================

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

// ============================================================
//  UI 元素
// ============================================================

/* 绘制圆角按钮（背景 + 居中文字；PR-G12 改走 DrawStringAt） */
void UiDrawButton(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const char *Text, UINT32 TextColor, UINT32 BgColor) {
    UINT32 TextLen = 0;
    UINT32 CellW;
    UINT32 CellH;
    UINT32 TextW;
    UINT32 TextX;
    UINT32 TextY;

    if (!Text) {
        Text = "";
    }
    UiFillRoundRectangle(X, Y, Width, Height, 5, BgColor);
    UiDrawRoundRectangle(X, Y, Width, Height, 5, COLOR_WHITE);
    if (Width > 2 && Height > 2) {
        UiDrawRoundRectangle(X + 1, Y + 1, Width - 2, Height - 2, 5, COLOR_DARK_GRAY);
    }

    while (Text[TextLen]) {
        TextLen++;
    }
    CellW = FontCellW();
    CellH = FontCellH();
    if (CellW == 0) {
        CellW = 8;
    }
    if (CellH == 0) {
        CellH = 16;
    }
    TextW = TextLen * CellW;
    TextX = X + 8;
    if (TextW + 16 < Width) {
        TextX = X + (Width - TextW) / 2;
    }
    TextY = Y + (Height > CellH ? (Height - CellH) / 2 : 0);
    HalVideoDrawStringAt(TextX, TextY, Text, TextColor);
}

/* 绘制水平进度条 */
void UiDrawProgressBar(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Progress, UINT32 MaxProgress, UINT32 Color, UINT32 BgColor) {
    UiFillRoundRectangle(X, Y, Width, Height, 3, BgColor);
    UiDrawRoundRectangle(X, Y, Width, Height, 3, COLOR_GRAY);

    if (MaxProgress == 0) {
        return;
    }
    {
        UINT32 FillWidth = (Progress * (Width - 4)) / MaxProgress;
        if (FillWidth > 0) {
            UiFillRoundRectangle(X + 2, Y + 2, FillWidth, Height - 4, 2, Color);
        }
    }
}

int UiHitRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Px, UINT32 Py) {
    return Px >= X && Py >= Y && Px < X + Width && Py < Y + Height;
}

void UiDrawListRow(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const char *Text,
                   int Selected, int Hovered) {
    UINT32 Fg = COLOR_BLACK;
    UINT32 Pad = 4;

    if (!Text) {
        Text = "";
    }
    if (Selected) {
        UiFillRectangle(X, Y, Width, Height, COLOR_BLUE);
        UiDrawRectangle(X, Y, Width, Height, COLOR_DARK_GRAY);
        Fg = COLOR_WHITE;
    } else if (Hovered) {
        UiFillRectangle(X, Y, Width, Height, COLOR_LIGHT_GRAY);
        UiDrawRectangle(X, Y, Width, Height, COLOR_GRAY);
        Fg = COLOR_BLACK;
    }
    if (Width > Pad * 2 && Height > 2) {
        HalVideoDrawStringAt(X + Pad, Y + (Height > FontCellH() ? (Height - FontCellH()) / 2 : 0),
                             Text, Fg);
    }
}

static void UiScrollThumb(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height,
                          int First, int Visible, int Total,
                          UINT32 *OutThumbY, UINT32 *OutThumbH) {
    UINT32 TrackH = Height;
    UINT32 ThumbH;
    UINT32 ThumbY;
    int MaxFirst;

    if (Total <= 0) {
        Total = 1;
    }
    if (Visible <= 0) {
        Visible = 1;
    }
    if (Visible >= Total) {
        ThumbH = TrackH;
        ThumbY = Y;
    } else {
        ThumbH = (TrackH * (UINT32)Visible) / (UINT32)Total;
        if (ThumbH < 12) {
            ThumbH = 12;
        }
        if (ThumbH > TrackH) {
            ThumbH = TrackH;
        }
        MaxFirst = Total - Visible;
        if (First < 0) {
            First = 0;
        }
        if (First > MaxFirst) {
            First = MaxFirst;
        }
        if (MaxFirst <= 0 || TrackH <= ThumbH) {
            ThumbY = Y;
        } else {
            ThumbY = Y + ((TrackH - ThumbH) * (UINT32)First) / (UINT32)MaxFirst;
        }
    }
    if (OutThumbY) {
        *OutThumbY = ThumbY;
    }
    if (OutThumbH) {
        *OutThumbH = ThumbH;
    }
}

void UiDrawScrollBar(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height,
                     int First, int Visible, int Total) {
    UINT32 ThumbY;
    UINT32 ThumbH;

    if (Width == 0 || Height == 0) {
        return;
    }
    UiFillRectangle(X, Y, Width, Height, COLOR_LIGHT_GRAY);
    UiDrawRectangle(X, Y, Width, Height, COLOR_GRAY);
    if (Total <= Visible || Total <= 0) {
        return;
    }
    UiScrollThumb(X, Y, Width, Height, First, Visible, Total, &ThumbY, &ThumbH);
    if (Width > 4 && ThumbH > 2) {
        UiFillRectangle(X + 2, ThumbY, Width > 4 ? Width - 4 : Width, ThumbH, COLOR_DARK_GRAY);
    }
}

int UiScrollBarHit(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height,
                   int First, int Visible, int Total,
                   UINT32 ClickX, UINT32 ClickY, int *OutFirst) {
    UINT32 ThumbY;
    UINT32 ThumbH;
    int MaxFirst;
    int Next;

    if (!OutFirst || !UiHitRect(X, Y, Width, Height, ClickX, ClickY)) {
        return 0;
    }
    if (Visible <= 0) {
        Visible = 1;
    }
    if (Total <= Visible) {
        *OutFirst = 0;
        return 1;
    }
    MaxFirst = Total - Visible;
    UiScrollThumb(X, Y, Width, Height, First, Visible, Total, &ThumbY, &ThumbH);

    if (ClickY < ThumbY) {
        Next = First - Visible;
    } else if (ClickY >= ThumbY + ThumbH) {
        Next = First + Visible;
    } else {
        /* 点在滑块上：按轨道比例跳转 */
        if (Height > ThumbH) {
            Next = (int)(((ClickY - Y) * (UINT32)MaxFirst) / (Height - ThumbH));
        } else {
            Next = First;
        }
    }
    if (Next < 0) {
        Next = 0;
    }
    if (Next > MaxFirst) {
        Next = MaxFirst;
    }
    *OutFirst = Next;
    return 1;
}

int UiListRowFromY(UINT32 ListTop, UINT32 LineH, int Visible, UINT32 ClickY) {
    int Row;

    if (LineH == 0 || ClickY < ListTop) {
        return -1;
    }
    Row = (int)((ClickY - ListTop) / LineH);
    if (Row < 0 || Row >= Visible) {
        return -1;
    }
    return Row;
}