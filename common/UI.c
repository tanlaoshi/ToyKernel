/*
 * UI.c — 帧缓冲图形与 UI 控件
 *
 * 提供直线、矩形、圆、三角形及按钮、进度条等绘制函数，供演示或扩展界面使用。
 */
#include "UI.h"
#include "Video.h"

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

/* 绘制直线（Bresenham 步进） */
void DrawLine(UINT32 X1, UINT32 Y1, UINT32 X2, UINT32 Y2, UINT32 Color) {
    int dx = (int)X2 - (int)X1;
    int dy = (int)Y2 - (int)Y1;
    int steps = (Abs(dx) > Abs(dy)) ? Abs(dx) : Abs(dy);
    
    if (steps == 0) {
        DrawPixel(X1, Y1, Color);
        return;
    }
    
    float xIncrement = (float)dx / steps;
    float yIncrement = (float)dy / steps;
    float x = X1;
    float y = Y1;
    
    for (int i = 0; i <= steps; i++) {
        DrawPixel((UINT32)x, (UINT32)y, Color);
        x += xIncrement;
        y += yIncrement;
    }
}

/* 绘制空心矩形边框 */
void DrawRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color) {
    if (Width < 2 || Height < 2) return;
    
    DrawLine(X, Y, X + Width - 1, Y, Color);
    DrawLine(X, Y + Height - 1, X + Width - 1, Y + Height - 1, Color);
    DrawLine(X, Y, X, Y + Height - 1, Color);
    DrawLine(X + Width - 1, Y, X + Width - 1, Y + Height - 1, Color);
}

/* 填充实心矩形 */
void FillRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color) {
    VideoFillRect(X, Y, Width, Height, Color);
}

/* 绘制圆角空心矩形 */
void DrawRoundRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Radius, UINT32 Color) {
    if (Radius > Width / 2) Radius = Width / 2;
    if (Radius > Height / 2) Radius = Height / 2;
    if (Radius == 0) {
        DrawRectangle(X, Y, Width, Height, Color);
        return;
    }
    
    // 画四条直线边
    DrawLine(X + Radius, Y, X + Width - Radius, Y, Color);
    DrawLine(X + Radius, Y + Height, X + Width - Radius, Y + Height, Color);
    DrawLine(X, Y + Radius, X, Y + Height - Radius, Color);
    DrawLine(X + Width, Y + Radius, X + Width, Y + Height - Radius, Color);
    
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
        DrawPixel(cx1 - x, cy1 - y, Color);
        DrawPixel(cx1 - y, cy1 - x, Color);
        // 右上角
        DrawPixel(cx2 + x, cy2 - y, Color);
        DrawPixel(cx2 + y, cy2 - x, Color);
        // 左下角
        DrawPixel(cx3 - x, cy3 + y, Color);
        DrawPixel(cx3 - y, cy3 + x, Color);
        // 右下角
        DrawPixel(cx4 + x, cy4 + y, Color);
        DrawPixel(cx4 + y, cy4 + x, Color);
        
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
void FillRoundRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Radius, UINT32 Color) {
    if (Radius > Width / 2) Radius = Width / 2;
    if (Radius > Height / 2) Radius = Height / 2;
    if (Radius == 0) {
        FillRectangle(X, Y, Width, Height, Color);
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
                DrawPixel(globalX, globalY, Color);
            }
        }
    }
}

// ============================================================
//  圆形
// ============================================================

/* 绘制空心圆（中点圆算法） */
void DrawCircle(UINT32 CenterX, UINT32 CenterY, UINT32 Radius, UINT32 Color) {
    if (Radius == 0) {
        DrawPixel(CenterX, CenterY, Color);
        return;
    }
    
    int x = 0;
    int y = (int)Radius;
    int d = 3 - 2 * (int)Radius;
    
    while (y >= x) {
        DrawPixel(CenterX + x, CenterY + y, Color);
        DrawPixel(CenterX - x, CenterY + y, Color);
        DrawPixel(CenterX + x, CenterY - y, Color);
        DrawPixel(CenterX - x, CenterY - y, Color);
        DrawPixel(CenterX + y, CenterY + x, Color);
        DrawPixel(CenterX - y, CenterY + x, Color);
        DrawPixel(CenterX + y, CenterY - x, Color);
        DrawPixel(CenterX - y, CenterY - x, Color);
        
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
void FillCircle(UINT32 CenterX, UINT32 CenterY, UINT32 Radius, UINT32 Color) {
    if (Radius == 0) {
        DrawPixel(CenterX, CenterY, Color);
        return;
    }
    
    for (int y = -(int)Radius; y <= (int)Radius; y++) {
        int x = ISqrt(Radius * Radius - y * y);
        for (int i = -x; i <= x; i++) {
            DrawPixel(CenterX + i, CenterY + y, Color);
        }
    }
}

// ============================================================
//  三角形（简化版）
// ============================================================

/* 绘制三角形边框（三条边） */
void DrawTriangle(UINT32 X1, UINT32 Y1, UINT32 X2, UINT32 Y2, UINT32 X3, UINT32 Y3, UINT32 Color) {
    DrawLine(X1, Y1, X2, Y2, Color);
    DrawLine(X2, Y2, X3, Y3, Color);
    DrawLine(X3, Y3, X1, Y1, Color);
}

/* 填充实心三角形（扫描线） */
void FillTriangle(UINT32 X1, UINT32 Y1, UINT32 X2, UINT32 Y2, UINT32 X3, UINT32 Y3, UINT32 Color) {
    DrawTriangle(X1, Y1, X2, Y2, X3, Y3, Color);
}

// ============================================================
//  UI 元素
// ============================================================

/* 绘制圆角按钮（背景 + 居中文字） */
void DrawButton(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const char *Text, UINT32 TextColor, UINT32 BgColor) {
    FillRoundRectangle(X, Y, Width, Height, 5, BgColor);
    DrawRoundRectangle(X, Y, Width, Height, 5, COLOR_WHITE);
    DrawRoundRectangle(X + 1, Y + 1, Width - 2, Height - 2, 5, COLOR_DARK_GRAY);
    
    UINT32 textLen = 0;
    while (Text[textLen]) textLen++;
    // Calculate text position for centered alignment (for future implementation)
    // UINT32 textX = X + (Width - textLen * 10) / 2;
    // UINT32 textY = Y + (Height - 16) / 2;
    DrawString(Text, TextColor);
}

/* 绘制水平进度条 */
void DrawProgressBar(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Progress, UINT32 MaxProgress, UINT32 Color, UINT32 BgColor) {
    FillRoundRectangle(X, Y, Width, Height, 3, BgColor);
    DrawRoundRectangle(X, Y, Width, Height, 3, COLOR_GRAY);
    
    UINT32 fillWidth = (Progress * (Width - 4)) / MaxProgress;
    if (fillWidth > 0) {
        FillRoundRectangle(X + 2, Y + 2, fillWidth, Height - 4, 2, Color);
    }
}