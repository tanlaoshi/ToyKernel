/*
 * EditUiInput.c — 点击与按键
 * 核心：EditUi.c
 */
#include "EditUiPrivate.h"

void EditUiOnClick(UINT32 X, UINT32 Y) {
    if (!EditUiIsFocused()) {
        return;
    }
    if (gSaveButtonHit &&
        X >= gEditSaveX && X < gEditSaveX + gSaveButtonWidth &&
        Y >= gEditSaveY && Y < gEditSaveY + gSaveButtonHeight) {
        EditUiSave();
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
