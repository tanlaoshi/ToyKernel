/*
 * ToyUiStyle.c — 用户态样式表实现（PR-UI-style-0）
 */
#include <ToyUiStyle.h>

static TOY_UI_STYLE s_Style;
static int s_Init;

static void InitDefault(void) {
    TOY_UI_STYLE *S = &s_Style;
    S->CheckBorder     = 0x00202020u;
    S->CheckFillOn     = 0x00208040u;
    S->CheckFillOff    = 0x00FFFFFFu;
    S->ListRowBg       = 0x00FFFFFFu;
    S->ListRowSelBg    = 0x00A0C8E8u;
    S->ListRowBorder   = 0x00606060u;
    S->FieldBg         = 0x00FFFFFFu;
    S->FieldBorder     = 0x00606060u;
    S->FieldBorderFocus= 0x002040C0u;
    s_Init = 1;
}

TOY_UI_STYLE ToyUiStyleDefault(void) {
    TOY_UI_STYLE S;
    S.CheckBorder     = 0x00202020u;
    S.CheckFillOn     = 0x00208040u;
    S.CheckFillOff    = 0x00FFFFFFu;
    S.ListRowBg       = 0x00FFFFFFu;
    S.ListRowSelBg    = 0x00A0C8E8u;
    S.ListRowBorder   = 0x00606060u;
    S.FieldBg         = 0x00FFFFFFu;
    S.FieldBorder     = 0x00606060u;
    S.FieldBorderFocus= 0x002040C0u;
    return S;
}

void ToyUiSetStyle(const TOY_UI_STYLE *Style) {
    if (!Style) {
        InitDefault();
        return;
    }
    s_Style = *Style;
    s_Init = 1;
}

const TOY_UI_STYLE *ToyUiStyleCurrent(void) {
    if (!s_Init) {
        InitDefault();
    }
    return &s_Style;
}
