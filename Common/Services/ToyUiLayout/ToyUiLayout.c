/*
 * ToyUiLayout.c — 用户态布局装载（编译进 Guest ELF，不进内核镜像）
 * 路径保留 Common/Services/… 以对验收；树迁后仍只由 Pkg EXTRA_OBJS 链接。
 */
#include "ToyUiLayout.h"
#include "toyos/syscall.h"

int ToyUiScreenWidth(void) {
    int W = 0;
    int H = 0;
    if (toy_screen_size(&W, &H) != 0 || W <= 0) {
        return 1280;
    }
    return W;
}

int ToyUiScreenHeight(void) {
    int W = 0;
    int H = 0;
    if (toy_screen_size(&W, &H) != 0 || H <= 0) {
        return 720;
    }
    return H;
}

/*
 * PR-UID-screen：只缩不放。设计画布 1280×720 为基准；屏比设计大 → scale=1
 * （窗口保持设计尺寸，居中留白）；屏比设计小 → 按宽高比取小者缩到放得下。
 * 旧 ScalePx(DesignW, ScreenW, DesignW)=ScreenW 会把窗口撑满整屏 →「现在很大」。
 */
static int FitScale(int ScreenW, int ScreenH, int DesignW, int DesignH) {
    int Sx;
    int Sy;

    if (DesignW <= 0 || DesignH <= 0) {
        return 1;
    }
    Sx = (ScreenW * 1000) / DesignW;
    Sy = (ScreenH * 1000) / DesignH;
    if (Sy < Sx) {
        Sx = Sy;
    }
    if (Sx > 1000) {
        Sx = 1000; /* 不放大 */
    }
    if (Sx < 1) {
        Sx = 1;
    }
    return Sx;
}

static int ApplyScale(int V, int Scale1000) {
    return (int)((long)V * (long)Scale1000 / 1000L);
}

static void ApplyFontTier(int Scale1000, int Unused) {
    (void)Unused;
    (void)ToyUiFontTier(Scale1000);
}

/* PR-UID-font：scale(千分) → 档位。<1500→1, <2500→2, <3500→3, 否则+1。
 * 只缩不放下 scale≤1000 → 恒为 1。App 可据此调 toy_set_font_id。 */
int ToyUiFontTier(int Scale1000) {
    int Tier;
    if (Scale1000 < 1500) {
        Tier = 1;
    } else if (Scale1000 < 2500) {
        Tier = 2;
    } else if (Scale1000 < 3500) {
        Tier = 3;
    } else {
        Tier = 3 + (Scale1000 - 3500) / 1000 + 1;
    }
    return Tier;
}

int ToyUiLoadWindow(const char *Title, int DesignW, int DesignH,
                    const TOY_UI_WIDGET *Widgets, int Count) {
    int ScreenW;
    int Scale;
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
    {
        int ScreenH = ToyUiScreenHeight();
        if (ScreenH <= 0) {
            ScreenH = 720;
        }
        Scale = FitScale(ScreenW, ScreenH, DesignW, DesignH);
    }
    ApplyFontTier(Scale, 1000);

    WinW = ApplyScale(DesignW, Scale);
    WinH = ApplyScale(DesignH, Scale);
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
        X = ApplyScale(Wgt->X, Scale);
        Y = ApplyScale(Wgt->Y, Scale);
        W = ApplyScale(Wgt->W, Scale);
        H = ApplyScale(Wgt->H, Scale);
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
