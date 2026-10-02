/*
 * EditUi.c — 简易编辑器核心（缓冲、开关、保存）
 * 绘制：EditUiPaint.c；按键：EditUiInput.c。
 */
#include "EditUiPrivate.h"

char gPath[EDIT_PATH_MAX];
char gBuf[EDIT_BUF_MAX];
UINTN gEditLen;
UINTN gCursor;
int gScrollLine;
int gEditDirty;
char gEditStatus[EDIT_STATUS_MAX];

/* PR-GUI-migrate-edit：Save 钮（SYNC → EditUiSave） */
UI_BUTTON_ACTION gEditSave;

static void EditSaveAction(void *Ctx) {
    (void)Ctx;
    EditUiSave();
}

static void EditSaveInit(void) {
    gEditSave.Button.X = 0;
    gEditSave.Button.Y = 0;
    gEditSave.Button.W = 72;
    gEditSave.Button.H = 24;
    gEditSave.Button.Text = "Save";
    gEditSave.Button.Enabled = 0;
    gEditSave.Button.Visible = 0;
    gEditSave.Button.m_State = UI_BUTTON_STATE_NORMAL;
    gEditSave.Kind = UI_ACTION_SYNC;
    gEditSave.Fn = EditSaveAction;
    gEditSave.Ctx = 0;
}

void EditSetStatus(const char *S) {
    CopyStr(gEditStatus, sizeof(gEditStatus), S ? S : "");
}

UINTN LineStartOf(UINTN Pos) {
    UINTN i = Pos;

    while (i > 0 && gBuf[i - 1] != '\n') {
        i--;
    }
    return i;
}

UINTN LineIndexOf(UINTN Pos) {
    UINTN i;
    UINTN Line = 0;

    for (i = 0; i < Pos && i < gEditLen; i++) {
        if (gBuf[i] == '\n') {
            Line++;
        }
    }
    return Line;
}

void EditClampScroll(UINT32 VisLines) {
    UINTN CurLine = LineIndexOf(gCursor);

    if (VisLines < 1) {
        VisLines = 1;
    }
    if ((UINTN)gScrollLine > CurLine) {
        gScrollLine = (int)CurLine;
    }
    if (CurLine >= (UINTN)gScrollLine + VisLines) {
        gScrollLine = (int)(CurLine - VisLines + 1);
    }
    if (gScrollLine < 0) {
        gScrollLine = 0;
    }
}

int InsertChar(char C) {
    UINTN i;

    if (gEditLen + 1 >= EDIT_BUF_MAX) {
        EditSetStatus("buffer full (2KiB)");
        return 0;
    }
    for (i = gEditLen; i > gCursor; i--) {
        gBuf[i] = gBuf[i - 1];
    }
    gBuf[gCursor] = C;
    gEditLen++;
    gCursor++;
    gBuf[gEditLen] = 0;
    gEditDirty = 1;
    EditSetStatus("modified");
    return 1;
}

void DeleteAt(UINTN Pos) {
    UINTN i;

    if (Pos >= gEditLen) {
        return;
    }
    for (i = Pos; i + 1 < gEditLen; i++) {
        gBuf[i] = gBuf[i + 1];
    }
    gEditLen--;
    gBuf[gEditLen] = 0;
    gEditDirty = 1;
    EditSetStatus("modified");
}

void EditUiSave(void) {
    int Err;

    if (gPath[0] == 0) {
        EditSetStatus("no path");
        EditUiRepaint();
        return;
    }
    Err = FileSystemWriteFile(gPath, gBuf, gEditLen);
    if (Err != FAT_OK) {
        EditSetStatus(FatStrError(Err));
    } else {
        gEditDirty = 0;
        EditSetStatus("saved");
    }
    EditUiRepaint();
}

void EditUiOpen(const char *Path) {
    UINTN N = 0;
    int Err;

    EditSaveInit();
    gEditLen = 0;
    gCursor = 0;
    gScrollLine = 0;
    gEditDirty = 0;
    gBuf[0] = 0;
    gPath[0] = 0;
    EditSetStatus("");

    if (!Path || !Path[0]) {
        EditSetStatus("bad path");
        return;
    }
    CopyStr(gPath, sizeof(gPath), Path);
    Err = FileSystemReadFile(gPath, gBuf, sizeof(gBuf) - 1, &N);
    if (Err == FAT_OK) {
        gEditLen = N;
        gBuf[gEditLen] = 0;
        EditSetStatus("ready  Ctrl+S save");
    } else if (Err == FAT_ERR_NOENT) {
        gEditLen = 0;
        gBuf[0] = 0;
        gEditDirty = 1;
        EditSetStatus("new file  Ctrl+S create");
    } else {
        EditSetStatus(FatStrError(Err));
    }
    gCursor = gEditLen;
}

int EditUiIsFocused(void) {
    return GuiFocusKind() == GUI_WIN_EDIT;
}
