/*
 * UI.h — 高级图形绘制接口
 *
 * 在 Video 帧缓冲上绘制几何图形与 UI 控件。颜色为 0x00RRGGBB 格式。
 */
#ifndef UI_H
#define UI_H

#include "BootTypes.h"

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
void UiDrawLine(UINT32 X1, UINT32 Y1, UINT32 X2, UINT32 Y2, UINT32 Color);
void UiDrawRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color);
void UiFillRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color);
void UiDrawRoundRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Radius, UINT32 Color);
void UiFillRoundRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Radius, UINT32 Color);
void UiDrawCircle(UINT32 CenterX, UINT32 CenterY, UINT32 Radius, UINT32 Color);
void UiFillCircle(UINT32 CenterX, UINT32 CenterY, UINT32 Radius, UINT32 Color);
void UiDrawTriangle(UINT32 X1, UINT32 Y1, UINT32 X2, UINT32 Y2, UINT32 X3, UINT32 Y3, UINT32 Color);
void UiFillTriangle(UINT32 X1, UINT32 Y1, UINT32 X2, UINT32 Y2, UINT32 X3, UINT32 Y3, UINT32 Color);

// 高级 UI 元素（PR-G12：按钮/列表行/滚动条；即时模式，无控件树）
void UiDrawButton(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const char *Text, UINT32 TextColor, UINT32 BgColor);
void UiDrawProgressBar(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Progress, UINT32 MaxProgress, UINT32 Color, UINT32 BgColor);
void UiDrawListRow(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const char *Text,
                   int Selected, int Hovered);
void UiDrawScrollBar(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height,
                     int First, int Visible, int Total);
/* 点中滚动条时更新 *First（页上/页下/按比例跳转）；返回 1=已处理 */
int UiScrollBarHit(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height,
                   int First, int Visible, int Total,
                   UINT32 ClickX, UINT32 ClickY, int *OutFirst);
/* 列表区 Y → 可见行号（0..Visible-1），越界返回 -1 */
int UiListRowFromY(UINT32 ListTop, UINT32 LineH, int Visible, UINT32 ClickY);
/* 点是否在矩形内（含左上、不含右下） */
int UiHitRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Px, UINT32 Py);

#endif