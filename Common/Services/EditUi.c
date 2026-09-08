/*
 * EditUi.c — 简易文本编辑器（PR-V2）
 *
 * Ctrl+S / Save 按钮 → FileSystemWriteFile；Esc 仅提示（关窗用标题 ×）。
 * 缓冲上限与 Files 预览同级（2KiB）；ASCII + 换行。
 */
#include "EditUi.h"
#include "Gui.h"
#include "FileSystem.h"
#include "Fat.h"
#include "Font.h"
#include "UI.h"
#include "Hal.h"
#include "Theme.h"

#define EDIT_PATH_MAX 96
#define EDIT_BUF_MAX  2048
#define EDIT_STATUS_MAX 64

static char gPath[EDIT_PATH_MAX];
static char gBuf[EDIT_BUF_MAX];
static UINTN gLen;
static UINTN gCursor;
static int gScrollLine;
static int gDirty;
static char gStatus[EDIT_STATUS_MAX];

static UINT32 gSaveX;
static UINT32 gSaveY;
static UINT32 gSaveW;
static UINT32 gSaveH;
static int gSaveHit;

static void CopyStr(char *Dst, int Max, const char *Src) {
    int i = 0;

    if (Max <= 0) {
        return;
    }
    if (!Src) {
        Dst[0] = 0;
        return;
    }
    while (Src[i] && i < Max - 1) {
        Dst[i] = Src[i];
        i++;
    }
    Dst[i] = 0;
}

static void SetStatus(const char *S) {
    CopyStr(gStatus, sizeof(gStatus), S ? S : "");
}

static void DrawLine(UINT32 X, UINT32 Y, const char *S, UINT32 Fg) {
    if (!S) {
        return;
    }
    HalVideoDrawStringAt(X, Y, S, Fg);
}

static UINTN LineStartOf(UINTN Pos) {
    UINTN i = Pos;

    while (i > 0 && gBuf[i - 1] != '\n') {
        i--;
    }
    return i;
}

static UINTN LineIndexOf(UINTN Pos) {
    UINTN i;
    UINTN Line = 0;

    for (i = 0; i < Pos && i < gLen; i++) {
        if (gBuf[i] == '\n') {
            Line++;
        }
    }
    return Line;
}

static void ClampScroll(UINT32 VisLines) {
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

static int InsertChar(char C) {
    UINTN i;

    if (gLen + 1 >= EDIT_BUF_MAX) {
        SetStatus("buffer full (2KiB)");
        return 0;
    }
    for (i = gLen; i > gCursor; i--) {
        gBuf[i] = gBuf[i - 1];
    }
    gBuf[gCursor] = C;
    gLen++;
    gCursor++;
    gBuf[gLen] = 0;
    gDirty = 1;
    SetStatus("modified");
    return 1;
}

static void DeleteAt(UINTN Pos) {
    UINTN i;

    if (Pos >= gLen) {
        return;
    }
    for (i = Pos; i + 1 < gLen; i++) {
        gBuf[i] = gBuf[i + 1];
    }
    gLen--;
    gBuf[gLen] = 0;
    gDirty = 1;
    SetStatus("modified");
}

void EditUiSave(void) {
    int Err;

    if (gPath[0] == 0) {
        SetStatus("no path");
        EditUiRepaint();
        return;
    }
    Err = FileSystemWriteFile(gPath, gBuf, gLen);
    if (Err != FAT_OK) {
        SetStatus(FatStrError(Err));
    } else {
        gDirty = 0;
        SetStatus("saved");
    }
    EditUiRepaint();
}

void EditUiOpen(const char *Path) {
    UINTN N = 0;
    int Err;

    gLen = 0;
    gCursor = 0;
    gScrollLine = 0;
    gDirty = 0;
    gBuf[0] = 0;
    gPath[0] = 0;
    SetStatus("");

    if (!Path || !Path[0]) {
        SetStatus("bad path");
        return;
    }
    CopyStr(gPath, sizeof(gPath), Path);
    Err = FileSystemReadFile(gPath, gBuf, sizeof(gBuf) - 1, &N);
    if (Err == FAT_OK) {
        gLen = N;
        gBuf[gLen] = 0;
        SetStatus("ready  Ctrl+S save");
    } else if (Err == FAT_ERR_NOENT) {
        gLen = 0;
        gBuf[0] = 0;
        gDirty = 1;
        SetStatus("new file  Ctrl+S create");
    } else {
        SetStatus(FatStrError(Err));
    }
    gCursor = gLen;
}

static void Paint(void) {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Bg;
    UINT32 LineH;
    UINT32 Ax;
    UINT32 Fw;
    UINT32 MaxCols;
    UINT32 VisLines;
    UINTN i;
    UINTN Line;
    UINTN Col;
    UINTN StartLine;
    char Row[96];
    char Head[100];
    UINT32 TextTop;
    UINT32 TextBot;
    UINT32 CurY;

    if (!GuiFocusClient(&X, &Y, &W, &H, &Bg)) {
        return;
    }
    GuiFrameBufferBegin();
    HalVideoFillRect(X, Y, W, H, Bg);
    HalVideoSetClipRegion(X, Y, W, H, Bg);

    LineH = FontAdvanceY();
    if (LineH < 16) {
        LineH = 16;
    }
    Ax = FontAdvanceX();
    if (Ax < 8) {
        Ax = 8;
    }

    Head[0] = 0;
    CopyStr(Head, sizeof(Head), gPath[0] ? gPath : "(no path)");
    DrawLine(X + 8, Y + 6, Head, COLOR_BLACK);
    DrawLine(X + 8, Y + 6 + LineH,
             gDirty ? "* dirty   Esc=hint  Ctrl+S=save" : "  clean   Esc=hint  Ctrl+S=save",
             COLOR_DARK_GRAY);

    gSaveW = 72;
    gSaveH = LineH + 8;
    gSaveX = X + W - gSaveW - 12;
    gSaveY = Y + 4;
    if (gSaveX > X + 8) {
        UiDrawButton(gSaveX, gSaveY, gSaveW, gSaveH, "Save", COLOR_BLACK, COLOR_WHITE);
        gSaveHit = 1;
    } else {
        gSaveHit = 0;
    }

    TextTop = Y + 6 + LineH * 2 + 4;
    TextBot = Y + H - LineH - 8;
    if (TextBot <= TextTop + LineH) {
        TextBot = TextTop + LineH;
    }
    VisLines = (TextBot - TextTop) / LineH;
    if (VisLines < 1) {
        VisLines = 1;
    }
    ClampScroll(VisLines);

    MaxCols = (W > 24) ? (W - 24) / Ax : 40;
    if (MaxCols > sizeof(Row) - 1) {
        MaxCols = sizeof(Row) - 1;
    }
    if (MaxCols < 8) {
        MaxCols = 8;
    }

    StartLine = (UINTN)gScrollLine;
    Line = 0;
    Col = 0;
    CurY = TextTop;
    Fw = 0;
    for (i = 0; i <= gLen && CurY + LineH <= TextBot; i++) {
        int AtCursor = (i == gCursor);
        char C = (i < gLen) ? gBuf[i] : 0;

        if (Line < StartLine) {
            if (i < gLen && C == '\n') {
                Line++;
                Col = 0;
            } else if (i < gLen) {
                Col++;
            }
            continue;
        }
        if (Line > StartLine + VisLines) {
            break;
        }

        if (AtCursor && Col < MaxCols) {
            HalVideoFillRect(X + 8 + (UINT32)Col * Ax, CurY, Ax, LineH, COLOR_DARK_GRAY);
        }

        if (i >= gLen) {
            break;
        }
        if (C == '\n' || Col >= MaxCols) {
            Row[Col] = 0;
            DrawLine(X + 8, CurY, Row, COLOR_BLACK);
            CurY += LineH;
            Line++;
            Col = 0;
            Fw = 0;
            if (C == '\n') {
                continue;
            }
        }
        if (C == '\r') {
            continue;
        }
        if (C >= 32 && C < 127) {
            Row[Col++] = C;
            Fw = 1;
        } else {
            Row[Col++] = '.';
            Fw = 1;
        }
    }
    if (Fw && Col > 0 && CurY + LineH <= TextBot) {
        Row[Col] = 0;
        DrawLine(X + 8, CurY, Row, COLOR_BLACK);
    }

    DrawLine(X + 8, Y + H - LineH - 4,
             gStatus[0] ? gStatus : " ", COLOR_DARK_GRAY);

    HalVideoClearClip();
    GuiBackupFocusWindow();
    GuiFrameBufferEnd();
}

void EditUiRepaint(void) {
    if (!EditUiIsFocused()) {
        return;
    }
    Paint();
}

void EditUiPaintFocused(void) {
    Paint();
}

void EditUiOnClick(UINT32 X, UINT32 Y) {
    if (!EditUiIsFocused()) {
        return;
    }
    if (gSaveHit &&
        X >= gSaveX && X < gSaveX + gSaveW &&
        Y >= gSaveY && Y < gSaveY + gSaveH) {
        EditUiSave();
    }
}

void EditUiOnEscape(void) {
    if (!EditUiIsFocused()) {
        return;
    }
    SetStatus(gDirty ? "dirty: Ctrl+S or close to discard" : "close via title X");
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
    for (i = 0; i <= gLen; i++) {
        if (Line == TargetLine) {
            Start = i;
            break;
        }
        if (i < gLen && gBuf[i] == '\n') {
            Line++;
        }
    }
    if (Line != TargetLine) {
        /* 无下一行 */
        if (Down) {
            gCursor = gLen;
        }
        EditUiRepaint();
        return;
    }
    gCursor = Start;
    for (i = 0; i < TargetCol && gCursor < gLen && gBuf[gCursor] != '\n'; i++) {
        gCursor++;
    }
    EditUiRepaint();
}

void EditUiOnArrowLeftRight(int Right) {
    if (!EditUiIsFocused()) {
        return;
    }
    if (Right) {
        if (gCursor < gLen) {
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

int EditUiIsFocused(void) {
    return GuiFocusKind() == GUI_WIN_EDIT;
}
