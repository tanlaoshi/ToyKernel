/*
 * UIDraw.c — PR-S3-ui-1：按钮 / 进度条
 */
#include "UI.h"
#include "HalVideo.h"
#include "Font.h"
#include "Theme.h"

/* 绘制圆角按钮（背景 + 居中文字；PR-G12 改走 DrawStringAt） */
void UiDrawButtonEx(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const char *Text,
                    UINT32 TextColor, UINT32 BgColor, int Hovered, int Pressed) {
    UINT32 TextLen = 0;
    UINT32 CellW;
    UINT32 CellH;
    UINT32 TextW;
    UINT32 TextX;
    UINT32 TextY;
    UINT32 Face = BgColor;
    UINT32 BorderOuter = ThemeWindowBorderFocus();
    UINT32 BorderInner = ThemeControlBorder();
    UINT32 Ox = 0;
    UINT32 Oy = 0;

    if (!Text) {
        Text = "";
    }
    /* 默认外框用控件边，悬停/按下才换成强调色（相对白 Focus 边更易辨认） */
    BorderOuter = ThemeControlBorder();
    if (Pressed) {
        Face = UiBlendRgb(BgColor, COLOR_BLACK, 96u);
        BorderOuter = ThemeControlAccent();
        BorderInner = ThemeControlAccent();
        Ox = 1;
        Oy = 1;
    } else if (Hovered) {
        /* Src=白盖在 Face 上：明显提亮 + 蓝框 */
        Face = UiBlendRgb(BgColor, COLOR_WHITE, 160u);
        BorderOuter = ThemeControlAccent();
        BorderInner = ThemeControlAccent();
    }
    UiFillRoundRectangle(X, Y, Width, Height, 5, Face);
    UiDrawRoundRectangle(X, Y, Width, Height, 5, BorderOuter);
    if (Width > 2 && Height > 2) {
        UiDrawRoundRectangle(X + 1, Y + 1, Width - 2, Height - 2, 5, BorderInner);
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
    TextX = X + 8 + Ox;
    if (TextW + 16 < Width) {
        TextX = X + (Width - TextW) / 2 + Ox;
    }
    TextY = Y + (Height > CellH ? (Height - CellH) / 2 : 0) + Oy;
    HalVideoDrawStringAt(TextX, TextY, Text, TextColor);
}

void UiDrawButton(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const char *Text,
                  UINT32 TextColor, UINT32 BgColor) {
    UiDrawButtonEx(X, Y, Width, Height, Text, TextColor, BgColor, 0, 0);
}

/* PR-GUI-btn-widget：禁用态。muted face + 灰边 + 灰文字，无偏移、无 blend。 */
void UiDrawButtonDisabled(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const char *Text) {
    UINT32 TextLen = 0;
    UINT32 CellW;
    UINT32 CellH;
    UINT32 TextW;
    UINT32 TextX;
    UINT32 TextY;
    UINT32 Face = ThemeButtonFaceDisabled();
    UINT32 BorderOuter = ThemeButtonBorderDisabled();
    UINT32 TextColor = ThemeButtonTextDisabled();

    if (!Text) {
        Text = "";
    }
    UiFillRoundRectangle(X, Y, Width, Height, 5, Face);
    UiDrawRoundRectangle(X, Y, Width, Height, 5, BorderOuter);
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
    UiDrawRoundRectangle(X, Y, Width, Height, 3, ThemeScrollBorder());

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
