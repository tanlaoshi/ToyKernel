/*
 * FilesUiKeys.c — 键盘与滚轮
 * 核心：FilesUi.c
 */
#include "FilesUiPrivate.h"

void FilesUiOnEscape(void) {
    if (!FilesUiIsFocused()) {
        return;
    }
    if (gMode == FILES_MODE_VIEW || gMode == FILES_MODE_CONFIRM ||
        gMode == FILES_MODE_PROMPT) {
        gMode = FILES_MODE_LIST;
        Paint();
        return;
    }
    if (gCwd[0]) {
        CwdPop();
        SetStatus("");
        (void)ReloadList();
        Paint();
    }
}

void FilesUiOnEnter(void) {
    if (!FilesUiIsFocused()) {
        return;
    }
    if (gMode == FILES_MODE_PROMPT) {
        DoPromptCommit();
        return;
    }
    if (gMode == FILES_MODE_CONFIRM) {
        DoDelete();
        return;
    }
    if (gMode != FILES_MODE_LIST) {
        return;
    }
    OpenSelected();
}

void FilesUiOnArrow(int Down) {
    if (!FilesUiIsFocused() || gMode != FILES_MODE_LIST || gCount <= 0) {
        return;
    }
    if (Down) {
        if (gSelected + 1 < gCount) {
            gSelected++;
        }
    } else {
        if (gSelected > 0) {
            gSelected--;
        }
    }
    gHoverIdx = gSelected;
    UpdatePreview();
    PaintList();
}

void FilesUiOnBackspace(void) {
    if (!FilesUiIsFocused() || gMode != FILES_MODE_PROMPT) {
        return;
    }
    if (gPromptLen > 0) {
        gPromptLen--;
        gPrompt[gPromptLen] = 0;
        Paint();
    }
}

void FilesUiOnChar(char C) {
    if (!FilesUiIsFocused()) {
        return;
    }

    if (gMode == FILES_MODE_CONFIRM) {
        if (C == 'y' || C == 'Y') {
            DoDelete();
        } else if (C == 'n' || C == 'N') {
            gMode = FILES_MODE_LIST;
            Paint();
        }
        return;
    }

    if (gMode == FILES_MODE_PROMPT) {
        if (C >= 32 && C < 127 && gPromptLen < FILES_NAME_MAX - 1) {
            gPrompt[gPromptLen++] = C;
            gPrompt[gPromptLen] = 0;
            Paint();
        }
        return;
    }

    if (gMode != FILES_MODE_LIST) {
        return;
    }
    if (C == 'd' || C == 'D') {
        BeginConfirmDelete();
    } else if (C == 'n' || C == 'N') {
        BeginPrompt(FILES_PROMPT_MKDIR);
    } else if (C == 'f' || C == 'F') {
        BeginPrompt(FILES_PROMPT_NEWFILE);
    } else if (C == 'r' || C == 'R') {
        BeginPrompt(FILES_PROMPT_RENAME);
    }
}

void FilesUiOnDeleteKey(void) {
    if (!FilesUiIsFocused() || gMode != FILES_MODE_LIST) {
        return;
    }
    BeginConfirmDelete();
}

void FilesUiOnWheel(INT8 Wheel) {
    int MaxScroll;
    int Next;

    if (Wheel == 0 || !FilesUiIsFocused() || gMode != FILES_MODE_LIST) {
        return;
    }
    if (gListVisible <= 0 || gCount <= gListVisible) {
        return;
    }
    /* 正滚轮 = 看列表上方 → gScroll 减小 */
    Next = gScroll - (int)Wheel;
    MaxScroll = gCount - gListVisible;
    if (MaxScroll < 0) {
        MaxScroll = 0;
    }
    if (Next < 0) {
        Next = 0;
    }
    if (Next > MaxScroll) {
        Next = MaxScroll;
    }
    if (Next == gScroll) {
        return;
    }
    gScroll = Next;
    if (gSelected < gScroll) {
        gSelected = gScroll;
    }
    if (gSelected >= gScroll + gListVisible) {
        gSelected = gScroll + gListVisible - 1;
    }
    PaintList();
}
