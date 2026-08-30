/*
 * UI.h — 高级图形绘制接口
 *
 * 在 Video 帧缓冲上绘制几何图形与 UI 控件。颜色为 0x00RRGGBB 格式。
 */
#ifndef UI_H
#define UI_H

#include "BootConfig.h"

// 颜色定义
#define COLOR_BLACK       0x00000000
#define COLOR_WHITE       0x00FFFFFF
#define COLOR_RED         0x00FF0000
#define COLOR_GREEN       0x0000FF00
#define COLOR_BLUE        0x000000FF
#define COLOR_YELLOW      0x00FFFF00
#define COLOR_CYAN        0x0000FFFF
#define COLOR_MAGENTA     0x00FF00FF
#define COLOR_GRAY        0x00808080
#define COLOR_DARK_GRAY   0x00404040
#define COLOR_LIGHT_GRAY  0x00C0C0C0

// 基本图形函数
void DrawLine(UINT32 X1, UINT32 Y1, UINT32 X2, UINT32 Y2, UINT32 Color);
void DrawRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color);
void FillRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color);
void DrawRoundRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Radius, UINT32 Color);
void FillRoundRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Radius, UINT32 Color);
void DrawCircle(UINT32 CenterX, UINT32 CenterY, UINT32 Radius, UINT32 Color);
void FillCircle(UINT32 CenterX, UINT32 CenterY, UINT32 Radius, UINT32 Color);
void DrawTriangle(UINT32 X1, UINT32 Y1, UINT32 X2, UINT32 Y2, UINT32 X3, UINT32 Y3, UINT32 Color);
void FillTriangle(UINT32 X1, UINT32 Y1, UINT32 X2, UINT32 Y2, UINT32 X3, UINT32 Y3, UINT32 Color);

// 高级 UI 元素
void DrawButton(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const char *Text, UINT32 TextColor, UINT32 BgColor);
void DrawProgressBar(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Progress, UINT32 MaxProgress, UINT32 Color, UINT32 BgColor);

#endif