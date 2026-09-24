/*
 * EditUiInput.c — 点击与按键
 * 核心：EditUi.c
 * PR-GUI-migrate-edit：Save 经 UiActionDispatch（press→release）；OnClick 不再触发 Save。
 */
#include "EditUiPrivate.h"

void EditUiOnClick(UINT32 X, UINT32 Y) {
    (void)X;
    (void)Y;
    /* Save 改走 OnPointer 抬起；保留入口以免 Gui 旧路径空调用炸 */
}

void EditUiOnPointer(UINT32 X, UINT32 Y, UINT8 Buttons) {
    static UINT8 sPrevBtn;
    int Hit;
    int Need = 0;
    UI_BUTTON_STATE Want;

    if (!EditUiIsFocused() || !gEditSave.Button.Visible) {
        sPrevBtn = Buttons;
        return;
    }

    Hit = UiButtonHit(&gEditSave.Button, X, Y);

    /* 非按下时维护 HOVER（UiButtonOnClick 不设 HOVER） */
    if (!(Buttons & 1u) &&
        gEditSave.Button.m_State != UI_BUTTON_STATE_PRESSED &&
        gEditSave.Button.Enabled) {
        Want = Hit ? UI_BUTTON_STATE_HOVER : UI_BUTTON_STATE_NORMAL;
        if (gEditSave.Button.m_State != Want) {
            gEditSave.Button.m_State = Want;
            Need = 1;
        }
    }

    if ((Buttons & 1u) && !(sPrevBtn & 1u)) {
        (void)UiActionDispatch(&gEditSave, 1, Hit);
        Need = 1;
    } else if (!(Buttons & 1u) && (sPrevBtn & 1u)) {
        if (UiActionDispatch(&gEditSave, 0, Hit)) {
            /* EditUiSave 已 Repaint；若仍命中补 HOVER 再刷一次 */
            if (Hit && gEditSave.Button.Enabled) {
                gEditSave.Button.m_State = UI_BUTTON_STATE_HOVER;
                Need = 1;
            }
        } else {
            Need = 1;
        }
    }

    sPrevBtn = Buttons;
    if (Need) {
        EditUiRepaint();
    }
}

void EditUiOnEscape(void) {
    if (!EditUiIsFocused()) {
        return;
    }
    EditSetStatus(gEditDirty ? "dirty: Ctrl+S or close to discard" : "close via title X");
    EditUiRepaint();
}

void EditUiOnEnter(void) {
    if (!EditUiIsFocused()) {
        return;
    }
    if (InsertChar('\n')) {
        EditUiRepaint();
    } else {
        EditUiRepaint();
    }
}

void EditUiOnArrow(int Down) {
    UINTN Line;
    UINTN Col;
    UINTN Start;
    UINTN i;
    UINTN TargetLine;
    UINTN TargetCol;

    if (!EditUiIsFocused()) {
        return;
    }
    Start = LineStartOf(gCursor);
    Col = gCursor - Start;
    Line = LineIndexOf(gCursor);
    if (Down) {
        TargetLine = Line + 1;
    } else {
        if (Line == 0) {
            gCursor = 0;
            EditUiRepaint();
            return;
        }
        TargetLine = Line - 1;
    }
    TargetCol = Col;
    Line = 0;
    Start = 0;
    for (i = 0; i <= gEditLen; i++) {
        if (Line == TargetLine) {
            Start = i;
            break;
        }
        if (i < gEditLen && gBuf[i] == '\n') {
            Line++;
        }
    }
    if (Line != TargetLine) {
        /* 无下一行 */
        if (Down) {
            gCursor = gEditLen;
        }
        EditUiRepaint();
        return;
    }
    gCursor = Start;
    for (i = 0; i < TargetCol && gCursor < gEditLen && gBuf[gCursor] != '\n'; i++) {
        gCursor++;
    }
    EditUiRepaint();
}

void EditUiOnArrowLeftRight(int Right) {
    if (!EditUiIsFocused()) {
        return;
    }
    if (Right) {
        if (gCursor < gEditLen) {
            gCursor++;
        }
    } else {
        if (gCursor > 0) {
            gCursor--;
        }
    }
    EditUiRepaint();
}

void EditUiOnBackspace(void) {
    if (!EditUiIsFocused()) {
        return;
    }
    if (gCursor == 0) {
        return;
    }
    gCursor--;
    DeleteAt(gCursor);
    EditUiRepaint();
}

void EditUiOnDeleteKey(void) {
    if (!EditUiIsFocused()) {
        return;
    }
    DeleteAt(gCursor);
    EditUiRepaint();
}

void EditUiOnChar(char C) {
    if (!EditUiIsFocused()) {
        return;
    }
    if (C < 32 || C >= 127) {
        return;
    }
    if (InsertChar(C)) {
        EditUiRepaint();
    } else {
        EditUiRepaint();
    }
}
