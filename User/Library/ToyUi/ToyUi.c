/*
 * ToyUi.c — libToyUi 核心：窗 / 标签 / 底栏按钮 / Poll（PR-S-toyui-1）
 */
#include <ToyUi.h>
#include <string.h>
#include <unistd.h>
#include <toyos/syscall.h>

#include "ToyUiPrivate.h"

static TOY_UI_WIN s_Wins[TOY_UI_WIN_MAX];

TOY_UI_WIN *ToyUiWinState(int WindowId) {
    if (WindowId < 0 || WindowId >= TOY_UI_WIN_MAX) {
        return 0;
    }
    return &s_Wins[WindowId];
}

int ToyUiInRect(unsigned X, unsigned Y, unsigned Rx, unsigned Ry, unsigned Rw,
                unsigned Rh) {
    return X >= Rx && Y >= Ry && X < Rx + Rw && Y < Ry + Rh;
}

void ToyUiCopyCap(char *Dst, unsigned Cap, const char *Src) {
    unsigned I;

    if (!Dst || Cap == 0) {
        return;
    }
    if (!Src) {
        Dst[0] = 0;
        return;
    }
    for (I = 0; I + 1 < Cap && Src[I]; I++) {
        Dst[I] = Src[I];
    }
    Dst[I] = 0;
}

int ToyUiCreateWindow(const char *Title, unsigned Width, unsigned Height) {
    int Wid;
    TOY_UI_WIN *St;

    Wid = create_window(Title, Width, Height);
    St = ToyUiWinState(Wid);
    if (St) {
        memset(St, 0, sizeof(*St));
    }
    return Wid;
}

int ToyUiSetLabel(int WindowId, const char *Text) {
    int Rc;

    Rc = ToyGfxDamageText(WindowId, Text);
    if (Rc == 0) {
        ToyUiRedrawWin(WindowId, ToyUiWinState(WindowId));
    }
    return Rc;
}

int ToyUiAddButton(int WindowId, int ButtonId, const char *Label) {
    long R;

    if (WindowId < 0 || ButtonId < TOY_UI_BUTTON_ID_MIN ||
        ButtonId > TOY_UI_BUTTON_ID_MAX || !Label) {
        return -1;
    }
    R = toy_ui_button(WindowId, ButtonId, Label);
    if (R < 0) {
        return -1;
    }
    ToyUiRedrawWin(WindowId, ToyUiWinState(WindowId));
    return 0;
}

int ToyUiPoll(int WindowId) {
    long R;
    unsigned X;
    unsigned Y;

    if (WindowId < 0) {
        return -1;
    }
    R = toy_poll_input(WindowId);
    if (R < 0) {
        return -1;
    }
    if (R >= TOY_UI_CLICK_PACK_BASE) {
        X = (unsigned)(R - TOY_UI_CLICK_PACK_BASE) & TOY_UI_CLICK_MASK;
        Y = (unsigned)(R - TOY_UI_CLICK_PACK_BASE) >> TOY_UI_CLICK_SHIFT;
        return ToyUiHitWidgets(WindowId, X, Y);
    }
    return (int)R;
}
