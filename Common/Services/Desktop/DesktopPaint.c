/*
 * DesktopPaint.c — 桌面绘制（图标/任务栏/开始菜单）（PR-S-desktop-split-2）
 *
 * 从 Desktop.c 迁出；只搬家、不改逻辑。
 */
#include "DesktopPrivate.h"

void FillRectFree(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, UINT32 Color) {
    UINT32 Row;
    UINT32 Col;
    UINT32 RunStart;
    int InRun;

    if (W == 0 || H == 0) {
        return;
    }
    for (Row = 0; Row < H; Row++) {
        InRun = 0;
        RunStart = 0;
        for (Col = 0; Col < W; Col++) {
            int Free = !PointOccupied(X + Col, Y + Row);
            if (Free && !InRun) {
                RunStart = Col;
                InRun = 1;
            } else if (!Free && InRun) {
                HalVideoFillRect(X + RunStart, Y + Row, Col - RunStart, 1, Color);
                InRun = 0;
            }
        }
        if (InRun) {
            HalVideoFillRect(X + RunStart, Y + Row, W - RunStart, 1, Color);
        }
    }
}

void DrawStringFree(UINT32 X, UINT32 Y, const char *Text, UINT32 Color) {
    UINT32 Cx = X;
    UINT32 CellH;

    if (!Text) {
        return;
    }
    CellH = FontCellH();
    while (*Text) {
        UINT32 Cp;
        UINTN N;
        UINT32 Adv;
        char One[5];
        UINTN k;
        UINT32 Row;
        UINT32 Col;
        int Free;

        N = Utf8Decode(Text, &Cp);
        if (N == 0) {
            Text++;
            continue;
        }
        Adv = FontCodepointAdvance(Cp);
        if (Adv == 0) {
            Adv = FontCellW();
        }
        for (k = 0; k < N && k < sizeof(One) - 1; k++) {
            One[k] = Text[k];
        }
        One[k] = 0;
        /*
         * 旧逻辑只测左上角：字形会画进标题栏留下黄/白烙印。
         * 单元格任一像素被窗占用则整字跳过。
         */
        Free = 1;
        for (Row = 0; Free && Row < CellH; Row++) {
            for (Col = 0; Col < Adv; Col++) {
                if (PointOccupied(Cx + Col, Y + Row)) {
                    Free = 0;
                    break;
                }
            }
        }
        if (Free) {
            HalVideoDrawStringAt(Cx, Y, One, Color);
        }
        Cx += Adv;
        Text += N;
    }
}

void DrawOneIconRaw(const DESKTOP_ICON *Icon, int Selected) {
    UINT32 LabelX;
    UINT32 LabelY;
    UINT32 LabelW;
    UINT32 Border;

    BlitIconFaceRaw(Icon->X, Icon->Y, Icon);
    Border = Selected ? ThemeIconSelect() : ThemeIconBorder();
    UiDrawRectangle(Icon->X, Icon->Y, DESKTOP_ICON_SIZE, DESKTOP_ICON_SIZE,
                    Border);
    if (Selected) {
        UiDrawRectangle(Icon->X + 1, Icon->Y + 1,
                        DESKTOP_ICON_SIZE - 2, DESKTOP_ICON_SIZE - 2,
                        ThemeIconSelect());
    }

    LabelW = Icon->Label ? FontStringWidth(Icon->Label) : 0;
    LabelX = Icon->X;
    if (LabelW < DESKTOP_ICON_SIZE) {
        LabelX = Icon->X + (DESKTOP_ICON_SIZE - LabelW) / 2;
    }
    LabelY = Icon->Y + DESKTOP_ICON_SIZE + DESKTOP_LABEL_PAD;
    if (Icon->Label) {
        HalVideoDrawStringAt(LabelX, LabelY, Icon->Label,
                             Selected ? ThemeIconSelect() : ThemeIconText());
    }
}

void DrawOneIconOccluded(const DESKTOP_ICON *Icon, int Selected) {
    UINT32 LabelX;
    UINT32 LabelY;
    UINT32 LabelW;
    UINT32 Border;

    BlitIconFaceFree(Icon->X, Icon->Y, Icon);
    Border = Selected ? ThemeIconSelect() : ThemeIconBorder();
    FillRectFree(Icon->X, Icon->Y, DESKTOP_ICON_SIZE, 1, Border);
    FillRectFree(Icon->X, Icon->Y + DESKTOP_ICON_SIZE - 1, DESKTOP_ICON_SIZE, 1,
                 Border);
    FillRectFree(Icon->X, Icon->Y, 1, DESKTOP_ICON_SIZE, Border);
    FillRectFree(Icon->X + DESKTOP_ICON_SIZE - 1, Icon->Y, 1, DESKTOP_ICON_SIZE,
                 Border);
    if (Selected) {
        FillRectFree(Icon->X + 1, Icon->Y + 1, DESKTOP_ICON_SIZE - 2, 1, Border);
        FillRectFree(Icon->X + 1, Icon->Y + DESKTOP_ICON_SIZE - 2,
                     DESKTOP_ICON_SIZE - 2, 1, Border);
    }

    LabelW = Icon->Label ? FontStringWidth(Icon->Label) : 0;
    LabelX = Icon->X;
    if (LabelW < DESKTOP_ICON_SIZE) {
        LabelX = Icon->X + (DESKTOP_ICON_SIZE - LabelW) / 2;
    }
    LabelY = Icon->Y + DESKTOP_ICON_SIZE + DESKTOP_LABEL_PAD;
    if (Icon->Label) {
        DrawStringFree(LabelX, LabelY, Icon->Label,
                       Selected ? ThemeIconSelect() : ThemeIconText());
    }
}
