/*
 * UILayout.c — PR-S3-ui-1：命中测试 / 列表行 / 滚动条
 */
#include "UI.h"
#include "HalVideo.h"
#include "Font.h"
#include "Theme.h"

int UiHitRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Px, UINT32 Py) {
    return Px >= X && Py >= Y && Px < X + Width && Py < Y + Height;
}

void UiDrawListRow(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const char *Text,
                   int Selected, int Hovered) {
    UINT32 Fg = ThemeText();
    UINT32 Pad = 4;
    char Fit[96];
    int i;
    UINT32 TextMaxW;

    if (!Text) {
        Text = "";
    }
    if (Selected) {
        UiFillRectangle(X, Y, Width, Height, ThemeControlAccent());
        UiDrawRectangle(X, Y, Width, Height, ThemeControlBorder());
        Fg = ThemeTextOnAccent();
    } else if (Hovered) {
        UiFillRectangle(X, Y, Width, Height, ThemeControlFace());
        UiDrawRectangle(X, Y, Width, Height, ThemeWindowTitleIdle());
        Fg = ThemeText();
    }
    if (Width > Pad * 2 && Height > 2) {
        TextMaxW = Width - Pad * 2u;
        for (i = 0; Text[i] && i < (int)sizeof(Fit) - 1; i++) {
            Fit[i] = Text[i];
        }
        Fit[i] = 0;
        UiFitTextUtf8(Fit, TextMaxW);
        HalVideoDrawStringAt(X + Pad, Y + (Height > FontCellH() ? (Height - FontCellH()) / 2 : 0),
                             Fit, Fg);
    }
}

static void UiScrollThumb(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height,
                          int First, int Visible, int Total,
                          UINT32 *OutThumbY, UINT32 *OutThumbH) {
    UINT32 TrackH = Height;
    UINT32 ThumbH;
    UINT32 ThumbY;
    int MaxFirst;

    /* 竖直滚动条：滑块 Y/H 只依赖轨道高度；X/Width 留给绘制侧 */
    (void)X;
    (void)Width;

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
    UiFillRectangle(X, Y, Width, Height, ThemeScrollTrack());
    UiDrawRectangle(X, Y, Width, Height, ThemeScrollBorder());
    if (Total <= Visible || Total <= 0) {
        return;
    }
    UiScrollThumb(X, Y, Width, Height, First, Visible, Total, &ThumbY, &ThumbH);
    if (Width > 4 && ThumbH > 2) {
        UiFillRectangle(X + 2, ThumbY, Width > 4 ? Width - 4 : Width, ThumbH, ThemeScrollThumb());
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
