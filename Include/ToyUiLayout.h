/*
 * ToyUiLayout.h — 编译期布局表 + LoadWindow（不改 ToyUi.h ABI）
 */
#ifndef TOY_UI_LAYOUT_H
#define TOY_UI_LAYOUT_H

#include "ToyUi.h"

typedef enum {
    TOY_UI_WIDGET_LABEL,
    TOY_UI_WIDGET_BUTTON,
    TOY_UI_WIDGET_CHECKBOX,
    TOY_UI_WIDGET_TEXTBOX
} TOY_UI_WIDGET_KIND;

typedef struct {
    TOY_UI_WIDGET_KIND Kind;
    int Id;
    int X, Y, W, H;
    const char *Text;
} TOY_UI_WIDGET;

/* 设计坐标 × scale = 物理像素；Scale = ScreenW / DesignW（整数除法） */
int ToyUiLoadWindow(const char *Title, int DesignW, int DesignH,
                    const TOY_UI_WIDGET *Widgets, int Count);

/* 屏宽；无查询时占位 1280（见 Tools/UiDesigner/README） */
int ToyUiScreenWidth(void);

#endif
