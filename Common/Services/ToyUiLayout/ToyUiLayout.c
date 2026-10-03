/*
 * ToyUiLayout.c — 用户态布局装载（编译进 Guest ELF，不进内核镜像）
 * 路径保留 Common/Services/… 以对验收；树迁后仍只由 Pkg EXTRA_OBJS 链接。
 */
#include "ToyUiLayout.h"

int ToyUiScreenWidth(void) {
    /* 占位：后续可走 Gui/Framebuffer 查询；三架构先固定设计宽 */
    return 1280;
}

static int ScalePx(int V, int ScreenW, int DesignW) {
    if (DesignW <= 0) {
        return V;
    }
    return (int)((long)V * (long)ScreenW / (long)DesignW);
}

static void ApplyFontTier(int ScreenW, int DesignW) {
    int Tier;

    /* 无公开 ToyUi 换字号 API：档位约定留给后续；避免破 ABI */
    if (DesignW <= 0) {
        return;
    }
    Tier = ScreenW / DesignW;
    if (ScreenW % DesignW != 0) {
        /* 1.5 → 当 1；用整数近似：ScreenW*2 >= DesignW*3 → tier 2 */
        if (ScreenW * 2 >= DesignW * 5) {
            Tier = 3;
        } else if (ScreenW * 2 >= DesignW * 3) {
            Tier = 2;
        } else {
            Tier = 1;
        }
    }
    if (Tier < 1) {
        Tier = 1;
    }
    (void)Tier;
}

int ToyUiLoadWindow(const char *Title, int DesignW, int DesignH,
                    const TOY_UI_WIDGET *Widgets, int Count) {
    int ScreenW;
    int WinW;
    int WinH;
    int Wid;
    int I;
    int X, Y, W, H;
    const TOY_UI_WIDGET *Wgt;

    if (!Title || DesignW <= 0 || DesignH <= 0 || Count < 0) {
        return -1;
    }
    if (Count > 0 && !Widgets) {
        return -1;
    }

    ScreenW = ToyUiScreenWidth();
    if (ScreenW <= 0) {
        ScreenW = 1280;
    }
    ApplyFontTier(ScreenW, DesignW);

    WinW = ScalePx(DesignW, ScreenW, DesignW);
    WinH = ScalePx(DesignH, ScreenW, DesignW);
    if (WinW < 64) {
        WinW = 64;
    }
    if (WinH < 48) {
        WinH = 48;
    }

    Wid = ToyUiCreateWindow(Title, (unsigned)WinW, (unsigned)WinH);
    if (Wid < 0) {
        return -1;
    }

    for (I = 0; I < Count; I++) {
        Wgt = &Widgets[I];
        X = ScalePx(Wgt->X, ScreenW, DesignW);
        Y = ScalePx(Wgt->Y, ScreenW, DesignW);
        W = ScalePx(Wgt->W, ScreenW, DesignW);
        H = ScalePx(Wgt->H, ScreenW, DesignW);
        if (W < 1) {
            W = 1;
        }
        if (H < 1) {
            H = 1;
        }
        switch (Wgt->Kind) {
        case TOY_UI_WIDGET_LABEL:
            ToyUiSetLabel(Wid, Wgt->Text ? Wgt->Text : "");
            break;
        case TOY_UI_WIDGET_BUTTON:
            if (Wgt->Id >= TOY_UI_BUTTON_ID_MIN &&
                Wgt->Id <= TOY_UI_BUTTON_ID_MAX) {
                ToyUiAddButton(Wid, Wgt->Id, Wgt->Text ? Wgt->Text : "");
            }
            (void)X;
            (void)Y;
            (void)W;
            (void)H;
            break;
        case TOY_UI_WIDGET_CHECKBOX:
            if (Wgt->Id >= 0 && Wgt->Id <= TOY_UI_CHECK_ID_MAX) {
                ToyUiAddCheckBox(Wid, Wgt->Id, (unsigned)X, (unsigned)Y);
            }
            break;
        case TOY_UI_WIDGET_TEXTBOX:
            if (Wgt->Id >= 0 && Wgt->Id <= TOY_UI_TEXT_ID_MAX) {
                ToyUiAddTextField(Wid, Wgt->Id, (unsigned)X, (unsigned)Y,
                                  (unsigned)W);
                if (Wgt->Text) {
                    ToyUiSetTextField(Wid, Wgt->Id, Wgt->Text);
                }
            }
            break;
        default:
            break;
        }
    }
    return Wid;
}
