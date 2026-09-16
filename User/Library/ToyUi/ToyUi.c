/*
 * ToyUi.c — libToyUi
 */
#include <ToyUi.h>
#include <string.h>
#include <unistd.h>
#include <toyos/syscall.h>

#define TOY_UI_WIN_MAX 6
#define TOY_UI_CLICK_PACK_BASE 400
#define TOY_UI_CLICK_SHIFT 10
#define TOY_UI_CLICK_MASK 1023
#define TOY_UI_CHECK_SIZE 16u
#define TOY_UI_LIST_ROW 18u
#define TOY_UI_FIELD_H 22u

typedef struct {
    int Used;
    unsigned X;
    unsigned Y;
    int Checked;
} TOY_UI_CHECK;

typedef struct {
    int Used;
    unsigned X;
    unsigned Y;
    unsigned W;
    int Count;
    int Selected;
    char Items[TOY_UI_LIST_ITEM_MAX][16];
} TOY_UI_LIST;

typedef struct {
    int Used;
    unsigned X;
    unsigned Y;
    unsigned W;
    int Focus;
    char Text[TOY_UI_TEXT_MAX];
} TOY_UI_FIELD;

typedef struct {
    TOY_UI_CHECK Check[TOY_UI_CHECK_ID_MAX + 1];
    TOY_UI_LIST List;
    TOY_UI_FIELD Field;
} TOY_UI_WIN;

static TOY_UI_WIN s_Wins[TOY_UI_WIN_MAX];

static TOY_UI_WIN *WinState(int WindowId) {
    if (WindowId < 0 || WindowId >= TOY_UI_WIN_MAX) {
        return 0;
    }
    return &s_Wins[WindowId];
}

static int InRect(unsigned X, unsigned Y, unsigned Rx, unsigned Ry, unsigned Rw,
                  unsigned Rh) {
    return X >= Rx && Y >= Ry && X < Rx + Rw && Y < Ry + Rh;
}

static void CopyCap(char *Dst, unsigned Cap, const char *Src) {
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

static void RedrawWin(int WindowId, TOY_UI_WIN *St) {
    int I;
    unsigned RowY;

    if (!St) {
        return;
    }
    for (I = 0; I <= TOY_UI_CHECK_ID_MAX; I++) {
        if (!St->Check[I].Used) {
            continue;
        }
        ToyGfxDrawRect(WindowId, St->Check[I].X, St->Check[I].Y,
                       TOY_UI_CHECK_SIZE, TOY_UI_CHECK_SIZE, 0x00202020u);
        if (St->Check[I].Checked) {
            ToyGfxFillRect(WindowId, St->Check[I].X + 3, St->Check[I].Y + 3,
                           TOY_UI_CHECK_SIZE - 6, TOY_UI_CHECK_SIZE - 6,
                           0x00208040u);
        } else {
            ToyGfxFillRect(WindowId, St->Check[I].X + 3, St->Check[I].Y + 3,
                           TOY_UI_CHECK_SIZE - 6, TOY_UI_CHECK_SIZE - 6,
                           TOY_GFX_COLOR_WHITE);
        }
    }
    if (St->List.Used) {
        for (I = 0; I < St->List.Count; I++) {
            RowY = St->List.Y + (unsigned)I * TOY_UI_LIST_ROW;
            if (I == St->List.Selected) {
                ToyGfxFillRect(WindowId, St->List.X, RowY, St->List.W,
                               TOY_UI_LIST_ROW - 2, 0x00A0C8E8u);
            } else {
                ToyGfxFillRect(WindowId, St->List.X, RowY, St->List.W,
                               TOY_UI_LIST_ROW - 2, TOY_GFX_COLOR_WHITE);
            }
            ToyGfxDrawRect(WindowId, St->List.X, RowY, St->List.W,
                           TOY_UI_LIST_ROW - 2, 0x00606060u);
        }
    }
    if (St->Field.Used) {
        ToyGfxFillRect(WindowId, St->Field.X, St->Field.Y, St->Field.W,
                       TOY_UI_FIELD_H, TOY_GFX_COLOR_WHITE);
        ToyGfxDrawRect(WindowId, St->Field.X, St->Field.Y, St->Field.W,
                       TOY_UI_FIELD_H,
                       St->Field.Focus ? 0x002040C0u : 0x00606060u);
    }
}

static int HitWidgets(int WindowId, unsigned X, unsigned Y) {
    TOY_UI_WIN *St;
    int I;
    unsigned RowY;

    St = WinState(WindowId);
    if (!St) {
        return TOY_UI_EVENT_CLICK;
    }
    for (I = 0; I <= TOY_UI_CHECK_ID_MAX; I++) {
        if (St->Check[I].Used &&
            InRect(X, Y, St->Check[I].X, St->Check[I].Y, TOY_UI_CHECK_SIZE,
                   TOY_UI_CHECK_SIZE)) {
            St->Check[I].Checked = !St->Check[I].Checked;
            RedrawWin(WindowId, St);
            return TOY_UI_CHECK_EVENT(I);
        }
    }
    if (St->List.Used) {
        for (I = 0; I < St->List.Count; I++) {
            RowY = St->List.Y + (unsigned)I * TOY_UI_LIST_ROW;
            if (InRect(X, Y, St->List.X, RowY, St->List.W, TOY_UI_LIST_ROW - 2)) {
                St->List.Selected = I;
                CopyCap(St->Field.Text, sizeof(St->Field.Text), St->List.Items[I]);
                RedrawWin(WindowId, St);
                return TOY_UI_LIST_EVENT(0);
            }
        }
    }
    if (St->Field.Used &&
        InRect(X, Y, St->Field.X, St->Field.Y, St->Field.W, TOY_UI_FIELD_H)) {
        St->Field.Focus = 1;
        RedrawWin(WindowId, St);
        return TOY_UI_TEXT_EVENT(0);
    }
    if (St->Field.Used) {
        St->Field.Focus = 0;
        RedrawWin(WindowId, St);
    }
    return TOY_UI_EVENT_CLICK;
}

int ToyUiCreateWindow(const char *Title, unsigned Width, unsigned Height) {
    int Wid;
    TOY_UI_WIN *St;

    Wid = create_window(Title, Width, Height);
    St = WinState(Wid);
    if (St) {
        memset(St, 0, sizeof(*St));
    }
    return Wid;
}

int ToyUiSetLabel(int WindowId, const char *Text) {
    int Rc;

    Rc = ToyGfxDamageText(WindowId, Text);
    if (Rc == 0) {
        RedrawWin(WindowId, WinState(WindowId));
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
    RedrawWin(WindowId, WinState(WindowId));
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
        return HitWidgets(WindowId, X, Y);
    }
    return (int)R;
}

int ToyUiAddCheckBox(int WindowId, int CheckId, unsigned X, unsigned Y) {
    TOY_UI_WIN *St;

    St = WinState(WindowId);
    if (!St || CheckId < 0 || CheckId > TOY_UI_CHECK_ID_MAX) {
        return -1;
    }
    St->Check[CheckId].Used = 1;
    St->Check[CheckId].X = X;
    St->Check[CheckId].Y = Y;
    St->Check[CheckId].Checked = 0;
    RedrawWin(WindowId, St);
    return 0;
}

int ToyUiSetCheckBox(int WindowId, int CheckId, int Checked) {
    TOY_UI_WIN *St;

    St = WinState(WindowId);
    if (!St || CheckId < 0 || CheckId > TOY_UI_CHECK_ID_MAX ||
        !St->Check[CheckId].Used) {
        return -1;
    }
    St->Check[CheckId].Checked = Checked ? 1 : 0;
    RedrawWin(WindowId, St);
    return 0;
}

int ToyUiGetCheckBox(int WindowId, int CheckId) {
    TOY_UI_WIN *St;

    St = WinState(WindowId);
    if (!St || CheckId < 0 || CheckId > TOY_UI_CHECK_ID_MAX ||
        !St->Check[CheckId].Used) {
        return -1;
    }
    return St->Check[CheckId].Checked;
}

int ToyUiAddList(int WindowId, int ListId, unsigned X, unsigned Y, unsigned W) {
    TOY_UI_WIN *St;

    St = WinState(WindowId);
    if (!St || ListId != 0 || W < 8) {
        return -1;
    }
    St->List.Used = 1;
    St->List.X = X;
    St->List.Y = Y;
    St->List.W = W;
    St->List.Count = 0;
    St->List.Selected = -1;
    RedrawWin(WindowId, St);
    return 0;
}

int ToyUiListAddItem(int WindowId, int ListId, const char *Text) {
    TOY_UI_WIN *St;

    St = WinState(WindowId);
    if (!St || ListId != 0 || !St->List.Used || !Text) {
        return -1;
    }
    if (St->List.Count >= TOY_UI_LIST_ITEM_MAX) {
        return -1;
    }
    CopyCap(St->List.Items[St->List.Count], sizeof(St->List.Items[0]), Text);
    St->List.Count++;
    RedrawWin(WindowId, St);
    return 0;
}

int ToyUiListSetSelected(int WindowId, int ListId, int Index) {
    TOY_UI_WIN *St;

    St = WinState(WindowId);
    if (!St || ListId != 0 || !St->List.Used) {
        return -1;
    }
    if (Index < 0 || Index >= St->List.Count) {
        return -1;
    }
    St->List.Selected = Index;
    RedrawWin(WindowId, St);
    return 0;
}

int ToyUiListGetSelected(int WindowId, int ListId) {
    TOY_UI_WIN *St;

    St = WinState(WindowId);
    if (!St || ListId != 0 || !St->List.Used) {
        return -1;
    }
    return St->List.Selected;
}

int ToyUiAddTextField(int WindowId, int FieldId, unsigned X, unsigned Y,
                      unsigned W) {
    TOY_UI_WIN *St;

    St = WinState(WindowId);
    if (!St || FieldId != 0 || W < 8) {
        return -1;
    }
    St->Field.Used = 1;
    St->Field.X = X;
    St->Field.Y = Y;
    St->Field.W = W;
    St->Field.Focus = 0;
    St->Field.Text[0] = 0;
    RedrawWin(WindowId, St);
    return 0;
}

int ToyUiSetTextField(int WindowId, int FieldId, const char *Text) {
    TOY_UI_WIN *St;

    St = WinState(WindowId);
    if (!St || FieldId != 0 || !St->Field.Used) {
        return -1;
    }
    CopyCap(St->Field.Text, sizeof(St->Field.Text), Text);
    RedrawWin(WindowId, St);
    return 0;
}

int ToyUiGetTextField(int WindowId, int FieldId, char *Buf, unsigned Cap) {
    TOY_UI_WIN *St;

    St = WinState(WindowId);
    if (!St || FieldId != 0 || !St->Field.Used || !Buf || Cap == 0) {
        return -1;
    }
    CopyCap(Buf, Cap, St->Field.Text);
    return 0;
}
