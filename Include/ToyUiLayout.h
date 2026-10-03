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

/* 屏宽/高；经 SYS_SCREEN_SIZE 查询，失败回退 1280/720（见 Tools/UiDesigner/README） */
int ToyUiScreenWidth(void);
int ToyUiScreenHeight(void);

/*
 * PR-UID-font：scale(千分) → 字号档位（1=默认, 2=×2, 3=×3）。
 * 只算档位；是否调 toy_set_font_id 由 App 自决（改全局字影响桌面/Shell）。
 */
int ToyUiFontTier(int Scale1000);

#endif
