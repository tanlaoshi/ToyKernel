/*
 * ToyUiWidgets.c — PR-S-toyui-1：复选框 / 列表 / 输入框
 *
 * 从 ToyUi.c 原样搬家；不改语义。
 */
#include <ToyUi.h>
#include <ToyGfx.h>

#include "ToyUiPrivate.h"

void ToyUiRedrawWin(int WindowId, TOY_UI_WIN *St) {
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

int ToyUiHitWidgets(int WindowId, unsigned X, unsigned Y) {
    TOY_UI_WIN *St;
    int I;
    unsigned RowY;

    St = ToyUiWinState(WindowId);
    if (!St) {
        return TOY_UI_EVENT_CLICK;
    }
    for (I = 0; I <= TOY_UI_CHECK_ID_MAX; I++) {
        if (St->Check[I].Used &&
            ToyUiInRect(X, Y, St->Check[I].X, St->Check[I].Y, TOY_UI_CHECK_SIZE,
                        TOY_UI_CHECK_SIZE)) {
            St->Check[I].Checked = !St->Check[I].Checked;
            ToyUiRedrawWin(WindowId, St);
            return TOY_UI_CHECK_EVENT(I);
        }
    }
    if (St->List.Used) {
        for (I = 0; I < St->List.Count; I++) {
            RowY = St->List.Y + (unsigned)I * TOY_UI_LIST_ROW;
            if (ToyUiInRect(X, Y, St->List.X, RowY, St->List.W,
                            TOY_UI_LIST_ROW - 2)) {
                St->List.Selected = I;
                ToyUiCopyCap(St->Field.Text, sizeof(St->Field.Text),
                             St->List.Items[I]);
                ToyUiRedrawWin(WindowId, St);
                return TOY_UI_LIST_EVENT(0);
            }
        }
    }
    if (St->Field.Used &&
        ToyUiInRect(X, Y, St->Field.X, St->Field.Y, St->Field.W, TOY_UI_FIELD_H)) {
        St->Field.Focus = 1;
        ToyUiRedrawWin(WindowId, St);
        return TOY_UI_TEXT_EVENT(0);
    }
    if (St->Field.Used) {
        St->Field.Focus = 0;
        ToyUiRedrawWin(WindowId, St);
    }
    return TOY_UI_EVENT_CLICK;
}

int ToyUiAddCheckBox(int WindowId, int CheckId, unsigned X, unsigned Y) {
    TOY_UI_WIN *St;

    St = ToyUiWinState(WindowId);
    if (!St || CheckId < 0 || CheckId > TOY_UI_CHECK_ID_MAX) {
        return -1;
    }
    St->Check[CheckId].Used = 1;
    St->Check[CheckId].X = X;
    St->Check[CheckId].Y = Y;
    St->Check[CheckId].Checked = 0;
    ToyUiRedrawWin(WindowId, St);
    return 0;
}

int ToyUiSetCheckBox(int WindowId, int CheckId, int Checked) {
    TOY_UI_WIN *St;

    St = ToyUiWinState(WindowId);
    if (!St || CheckId < 0 || CheckId > TOY_UI_CHECK_ID_MAX ||
        !St->Check[CheckId].Used) {
        return -1;
    }
    St->Check[CheckId].Checked = Checked ? 1 : 0;
    ToyUiRedrawWin(WindowId, St);
    return 0;
}

int ToyUiGetCheckBox(int WindowId, int CheckId) {
    TOY_UI_WIN *St;

    St = ToyUiWinState(WindowId);
    if (!St || CheckId < 0 || CheckId > TOY_UI_CHECK_ID_MAX ||
        !St->Check[CheckId].Used) {
        return -1;
    }
    return St->Check[CheckId].Checked;
}

int ToyUiAddList(int WindowId, int ListId, unsigned X, unsigned Y, unsigned W) {
    TOY_UI_WIN *St;

    St = ToyUiWinState(WindowId);
    if (!St || ListId != 0 || W < 8) {
        return -1;
    }
    St->List.Used = 1;
    St->List.X = X;
    St->List.Y = Y;
    St->List.W = W;
    St->List.Count = 0;
    St->List.Selected = -1;
    ToyUiRedrawWin(WindowId, St);
    return 0;
}

int ToyUiListAddItem(int WindowId, int ListId, const char *Text) {
    TOY_UI_WIN *St;

    St = ToyUiWinState(WindowId);
    if (!St || ListId != 0 || !St->List.Used || !Text) {
        return -1;
    }
    if (St->List.Count >= TOY_UI_LIST_ITEM_MAX) {
        return -1;
    }
    ToyUiCopyCap(St->List.Items[St->List.Count], sizeof(St->List.Items[0]), Text);
    St->List.Count++;
    ToyUiRedrawWin(WindowId, St);
    return 0;
}

int ToyUiListSetSelected(int WindowId, int ListId, int Index) {
    TOY_UI_WIN *St;

    St = ToyUiWinState(WindowId);
    if (!St || ListId != 0 || !St->List.Used) {
        return -1;
    }
    if (Index < 0 || Index >= St->List.Count) {
        return -1;
    }
    St->List.Selected = Index;
    ToyUiRedrawWin(WindowId, St);
    return 0;
}

int ToyUiListGetSelected(int WindowId, int ListId) {
    TOY_UI_WIN *St;

    St = ToyUiWinState(WindowId);
    if (!St || ListId != 0 || !St->List.Used) {
        return -1;
    }
    return St->List.Selected;
}

int ToyUiAddTextField(int WindowId, int FieldId, unsigned X, unsigned Y,
                      unsigned W) {
    TOY_UI_WIN *St;

    St = ToyUiWinState(WindowId);
    if (!St || FieldId != 0 || W < 8) {
        return -1;
    }
    St->Field.Used = 1;
    St->Field.X = X;
    St->Field.Y = Y;
    St->Field.W = W;
    St->Field.Focus = 0;
    St->Field.Text[0] = 0;
    ToyUiRedrawWin(WindowId, St);
    return 0;
}

int ToyUiSetTextField(int WindowId, int FieldId, const char *Text) {
    TOY_UI_WIN *St;

    St = ToyUiWinState(WindowId);
    if (!St || FieldId != 0 || !St->Field.Used) {
        return -1;
    }
    ToyUiCopyCap(St->Field.Text, sizeof(St->Field.Text), Text);
    ToyUiRedrawWin(WindowId, St);
    return 0;
}

int ToyUiGetTextField(int WindowId, int FieldId, char *Buf, unsigned Cap) {
    TOY_UI_WIN *St;

    St = ToyUiWinState(WindowId);
    if (!St || FieldId != 0 || !St->Field.Used || !Buf || Cap == 0) {
        return -1;
    }
    ToyUiCopyCap(Buf, Cap, St->Field.Text);
    return 0;
}

/* 无 Shift：字母小写、数字与空白；供焦点输入框 */
static char ToyUiHidAscii(int Hid) {
    if (Hid >= TOY_UI_HID_A && Hid <= 0x1D) {
        return (char)('a' + (Hid - TOY_UI_HID_A));
    }
    if (Hid >= 0x1E && Hid <= 0x26) {
        return (char)('1' + (Hid - 0x1E));
    }
    if (Hid == 0x27) {
        return '0';
    }
    if (Hid == TOY_UI_HID_SPACE) {
        return ' ';
    }
    return 0;
}

int ToyUiApplyKeyToField(int WindowId, int Hid) {
    TOY_UI_WIN *St;
    char C;
    unsigned Len;

    St = ToyUiWinState(WindowId);
    if (!St || !St->Field.Used || !St->Field.Focus) {
        return 0;
    }
    if (Hid == TOY_UI_HID_BACKSPACE) {
        Len = 0;
        while (St->Field.Text[Len]) {
            Len++;
        }
        if (Len > 0) {
            St->Field.Text[Len - 1] = 0;
            ToyUiRedrawWin(WindowId, St);
        }
        return 1;
    }
    C = ToyUiHidAscii(Hid);
    if (!C) {
        return 0;
    }
    Len = 0;
    while (St->Field.Text[Len]) {
        Len++;
    }
    if (Len + 1 >= TOY_UI_TEXT_MAX) {
        return 1;
    }
    St->Field.Text[Len] = C;
    St->Field.Text[Len + 1] = 0;
    ToyUiRedrawWin(WindowId, St);
    return 1;
}
