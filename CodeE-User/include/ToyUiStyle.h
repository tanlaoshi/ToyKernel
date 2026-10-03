/*
 * ToyUiStyle.h — 用户态样式表（PR-UI-style-0）
 *
 * 纯数据结构；自绘控件读表。不改 ToyUi.h / ToyGfx.h ABI。
 * 内核 Theme 仍管窗口边框/桌面；本表只管控件客户区外观。
 */
#ifndef TOY_UI_STYLE_H
#define TOY_UI_STYLE_H

typedef struct {
    /* 复选框 */
    unsigned CheckBorder;
    unsigned CheckFillOn;
    unsigned CheckFillOff;
    /* 列表 */
    unsigned ListRowBg;
    unsigned ListRowSelBg;
    unsigned ListRowBorder;
    /* 输入框 */
    unsigned FieldBg;
    unsigned FieldBorder;
    unsigned FieldBorderFocus;
} TOY_UI_STYLE;

/* 默认表（与原硬编码一致） */
TOY_UI_STYLE ToyUiStyleDefault(void);

/* 设当前样式；之后 ToyUiRedrawWin 读本表。NULL 恢复默认。 */
void ToyUiSetStyle(const TOY_UI_STYLE *Style);

/* 取当前样式指针（恒非空） */
const TOY_UI_STYLE *ToyUiStyleCurrent(void);

#endif
