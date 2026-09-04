/*
 * FilesUi.c — 文件浏览器（PR-FB1）
 */
#include "FilesUi.h"
#include "Gui.h"
#include "FileSystem.h"
#include "Process.h"
#include "Theme.h"
#include "Font.h"
#include "UI.h"
#include "Hal.h"
#include "Debug.h"

#define FILES_PATH_MAX     96
#define FILES_VIEW_MAX     2048
#define FILES_DBLCLICK_MAX 2000000ULL
#define FILES_DBLCLICK_SLOP 16u

typedef enum {
    FILES_MODE_LIST = 0,
    FILES_MODE_VIEW
} FILES_MODE;

static char gCwd[FILES_PATH_MAX];
static FAT_DIR_ENT gEnts[FAT_LIST_MAX];
static int gCount;
static int gSelected;
static int gScroll;
static FILES_MODE gMode;
static char gView[FILES_VIEW_MAX];
static UINTN gViewLen;
static char gViewTitle[FAT_ENT_NAME_MAX];

static int gClickSel = -1;
static UINT64 gClickClock;
static UINT32 gClickX;
static UINT32 gClickY;

static UINT64 FilesClock(void) {
    return HalCpuTicks(0);
}

static int FocusFilesWindow(void) {
    int i;

    if (GuiFocusKind() == GUI_WIN_FILES) {
        return 1;
    }
    for (i = 0; i < GUI_MAX_WINS; i++) {
        if (GuiWindowKind(i) == GUI_WIN_FILES) {
            GuiRaiseToFront(i);
            return 1;
        }
    }
    return 0;
}

static int StrEqIgnoreCase(const char *A, const char *B) {
    while (*A && *B) {
        char Ca = *A;
        char Cb = *B;
        if (Ca >= 'A' && Ca <= 'Z') {
            Ca = (char)(Ca - 'A' + 'a');
        }
        if (Cb >= 'A' && Cb <= 'Z') {
            Cb = (char)(Cb - 'A' + 'a');
        }
        if (Ca != Cb) {
            return 0;
        }
        A++;
        B++;
    }
    return *A == 0 && *B == 0;
}

static int EndsWithElf(const char *Name) {
    int N = 0;
    while (Name[N]) {
        N++;
    }
    if (N < 4) {
        return 0;
    }
    return StrEqIgnoreCase(Name + N - 4, ".elf");
}

static void CopyStr(char *Dst, int Max, const char *Src) {
    int i;
    if (Max <= 0) {
        return;
    }
    for (i = 0; Src && Src[i] && i < Max - 1; i++) {
        Dst[i] = Src[i];
    }
    Dst[i] = 0;
}

static int JoinPath(char *Out, int Max, const char *Dir, const char *Name) {
    int i = 0;
    int j;

    if (!Out || Max <= 0 || !Name) {
        return 0;
    }
    if (Dir && Dir[0]) {
        for (j = 0; Dir[j] && i < Max - 1; j++) {
            Out[i++] = Dir[j];
        }
        if (i < Max - 1 && (i == 0 || Out[i - 1] != '/')) {
            Out[i++] = '/';
        }
    }
    for (j = 0; Name[j] && i < Max - 1; j++) {
        Out[i++] = Name[j];
    }
    Out[i] = 0;
    return i > 0;
}

static void CwdPop(void) {
    int i;
    int Last = -1;

    for (i = 0; gCwd[i]; i++) {
        if (gCwd[i] == '/') {
            Last = i;
        }
    }
    if (Last < 0) {
        gCwd[0] = 0;
        return;
    }
    gCwd[Last] = 0;
}

static int ReloadList(void) {
    int Err;

    gCount = 0;
    gSelected = 0;
    gScroll = 0;
    Err = FsListEntries(gCwd[0] ? gCwd : "", gEnts, FAT_LIST_MAX, &gCount);
    if (Err != FAT_OK) {
        gCount = 0;
        DebugWrite("files: list err\n");
        return Err;
    }
    return FAT_OK;
}

static void DrawLine(UINT32 X, UINT32 Y, const char *S, UINT32 Fg) {
    if (!S) {
        return;
    }
    HalVideoDrawStringAt(X, Y, S, Fg);
}

static void PaintList(void) {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Bg;
    UINT32 LineH;
    UINT32 RowY;
    int Visible;
    int i;
    char Line[96];
    char PathShow[FILES_PATH_MAX + 8];

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

    PathShow[0] = 0;
    CopyStr(PathShow, sizeof(PathShow), "Path: ");
    {
        int n = 0;
        while (PathShow[n]) {
            n++;
        }
        if (gCwd[0]) {
            CopyStr(PathShow + n, (int)sizeof(PathShow) - n, gCwd);
        } else {
            CopyStr(PathShow + n, (int)sizeof(PathShow) - n, "/");
        }
    }
    DrawLine(X + 8, Y + 8, PathShow, COLOR_BLACK);
    DrawLine(X + 8, Y + 8 + LineH,
             "Enter/dblclick open  Esc=up  (read-only)", COLOR_DARK_GRAY);

    Visible = 0;
    if (H > 8 + LineH * 3) {
        Visible = (int)((H - 8 - LineH * 3) / LineH);
    }
    if (Visible < 1) {
        Visible = 1;
    }
    if (gSelected < gScroll) {
        gScroll = gSelected;
    }
    if (gSelected >= gScroll + Visible) {
        gScroll = gSelected - Visible + 1;
    }
    if (gScroll < 0) {
        gScroll = 0;
    }

    RowY = Y + 8 + LineH * 2 + 4;
    for (i = 0; i < Visible && gScroll + i < gCount; i++) {
        const FAT_DIR_ENT *E = &gEnts[gScroll + i];
        int Idx = gScroll + i;
        UINT32 RowBg = (Idx == gSelected) ? COLOR_YELLOW : Bg;
        UINT32 Fg = COLOR_BLACK;
        int k = 0;
        int j;

        if (Idx == gSelected) {
            HalVideoFillRect(X + 4, RowY, W > 8 ? W - 8 : W, LineH, RowBg);
        }
        if (E->Attr & FAT_ATTR_DIR) {
            Line[k++] = '[';
            Line[k++] = 'D';
            Line[k++] = ']';
            Line[k++] = ' ';
        } else {
            Line[k++] = ' ';
            Line[k++] = ' ';
            Line[k++] = ' ';
            Line[k++] = ' ';
        }
        for (j = 0; E->Name[j] && k < (int)sizeof(Line) - 1; j++) {
            Line[k++] = E->Name[j];
        }
        Line[k] = 0;
        DrawLine(X + 8, RowY, Line, Fg);
        RowY += LineH;
    }
    if (gCount == 0) {
        DrawLine(X + 8, RowY, "(empty)", COLOR_DARK_GRAY);
    }

    HalVideoClearClip();
    GuiBackupFocusWindow();
    GuiFrameBufferEnd();
}

static void PaintView(void) {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Bg;
    UINT32 LineH;
    UINT32 CurY;
    UINTN i;
    char Title[80];
    int ti;

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

    Title[0] = 0;
    CopyStr(Title, sizeof(Title), "View: ");
    ti = 0;
    while (Title[ti]) {
        ti++;
    }
    CopyStr(Title + ti, (int)sizeof(Title) - ti, gViewTitle);
    DrawLine(X + 8, Y + 8, Title, COLOR_BLACK);
    DrawLine(X + 8, Y + 8 + LineH, "Esc = back to list", COLOR_DARK_GRAY);

    CurY = Y + 8 + LineH * 2 + 4;
    {
        char Row[96];
        int Col = 0;
        UINT32 MaxCols = (W > 16) ? (W - 16) / (FontAdvanceX() ? FontAdvanceX() : 8) : 40;

        if (MaxCols > sizeof(Row) - 1) {
            MaxCols = sizeof(Row) - 1;
        }
        if (MaxCols < 8) {
            MaxCols = 8;
        }
        for (i = 0; i < gViewLen && CurY + LineH <= Y + H; i++) {
            char C = gView[i];
            if (C == '\n' || Col >= (int)MaxCols) {
                Row[Col] = 0;
                DrawLine(X + 8, CurY, Row, COLOR_BLACK);
                CurY += LineH;
                Col = 0;
                if (C == '\n') {
                    continue;
                }
            }
            if (C == '\r') {
                continue;
            }
            if (C >= 32 && C < 127) {
                Row[Col++] = C;
            } else {
                Row[Col++] = '.';
            }
        }
        if (Col > 0 && CurY + LineH <= Y + H) {
            Row[Col] = 0;
            DrawLine(X + 8, CurY, Row, COLOR_BLACK);
        }
    }

    HalVideoClearClip();
    GuiBackupFocusWindow();
    GuiFrameBufferEnd();
}

static void Paint(void) {
    if (!FocusFilesWindow()) {
        return;
    }
    if (gMode == FILES_MODE_VIEW) {
        PaintView();
    } else {
        PaintList();
    }
}

static void OpenSelected(void) {
    FAT_DIR_ENT *E;
    char Path[FILES_PATH_MAX];

    if (gMode != FILES_MODE_LIST || gSelected < 0 || gSelected >= gCount) {
        return;
    }
    E = &gEnts[gSelected];
    if (E->Attr & FAT_ATTR_DIR) {
        if (E->Name[0] == '.' && E->Name[1] == 0) {
            return;
        }
        if (E->Name[0] == '.' && E->Name[1] == '.' && E->Name[2] == 0) {
            CwdPop();
        } else {
            if (!JoinPath(Path, sizeof(Path), gCwd, E->Name)) {
                return;
            }
            CopyStr(gCwd, sizeof(gCwd), Path);
        }
        gMode = FILES_MODE_LIST;
        (void)ReloadList();
        Paint();
        return;
    }

    if (!JoinPath(Path, sizeof(Path), gCwd, E->Name)) {
        return;
    }
    if (EndsWithElf(E->Name)) {
        DebugWrite("files: exec ");
        DebugWrite(Path);
        DebugWrite("\n");
        if (ProcessExec(Path) != 0) {
            HalConsoleWriteSerial("files: exec failed\n");
        }
        return;
    }

    gViewLen = 0;
    if (FsReadFile(Path, gView, sizeof(gView) - 1, &gViewLen) != FAT_OK) {
        HalConsoleWriteSerial("files: read failed\n");
        return;
    }
    gView[gViewLen] = 0;
    CopyStr(gViewTitle, sizeof(gViewTitle), E->Name);
    gMode = FILES_MODE_VIEW;
    Paint();
}

void FilesUiOpen(void) {
    gCwd[0] = 0;
    gMode = FILES_MODE_LIST;
    gClickSel = -1;
    (void)ReloadList();
    Paint();
}

void FilesUiRepaint(void) {
    Paint();
}

void FilesUiPaintFocused(void) {
    if (GuiFocusKind() != GUI_WIN_FILES) {
        return;
    }
    if (gMode == FILES_MODE_VIEW) {
        PaintView();
    } else {
        PaintList();
    }
}

void FilesUiRefresh(void) {
    if (GuiFocusKind() != GUI_WIN_FILES && !FilesUiIsFocused()) {
        int i;
        int Found = 0;
        for (i = 0; i < GUI_MAX_WINS; i++) {
            if (GuiWindowKind(i) == GUI_WIN_FILES) {
                Found = 1;
                break;
            }
        }
        if (!Found) {
            return;
        }
    }
    if (gMode == FILES_MODE_LIST) {
        (void)ReloadList();
    }
    Paint();
}

void FilesUiOnClick(UINT32 X, UINT32 Y) {
    UINT32 Cx;
    UINT32 Cy;
    UINT32 Cw;
    UINT32 Ch;
    UINT32 Bg;
    UINT32 LineH;
    UINT32 ListTop;
    int Visible;
    int Row;
    int Idx;
    UINT64 Now;
    UINT64 Dt;
    UINT32 Dx;
    UINT32 Dy;

    if (gMode == FILES_MODE_VIEW) {
        return;
    }
    if (!GuiFocusClient(&Cx, &Cy, &Cw, &Ch, &Bg)) {
        return;
    }
    if (X < Cx || Y < Cy || X >= Cx + Cw || Y >= Cy + Ch) {
        return;
    }

    LineH = FontAdvanceY();
    if (LineH < 16) {
        LineH = 16;
    }
    ListTop = Cy + 8 + LineH * 2 + 4;
    if (Y < ListTop) {
        return;
    }
    Visible = 0;
    if (Ch > 8 + LineH * 3) {
        Visible = (int)((Ch - 8 - LineH * 3) / LineH);
    }
    if (Visible < 1) {
        Visible = 1;
    }
    Row = (int)((Y - ListTop) / LineH);
    if (Row < 0 || Row >= Visible) {
        return;
    }
    Idx = gScroll + Row;
    if (Idx < 0 || Idx >= gCount) {
        return;
    }

    Now = FilesClock();
    Dt = (Now >= gClickClock) ? (Now - gClickClock) : FILES_DBLCLICK_MAX + 1;
    Dx = (X >= gClickX) ? (X - gClickX) : (gClickX - X);
    Dy = (Y >= gClickY) ? (Y - gClickY) : (gClickY - Y);

    if (Idx == gClickSel &&
        Dt <= FILES_DBLCLICK_MAX &&
        Dx <= FILES_DBLCLICK_SLOP &&
        Dy <= FILES_DBLCLICK_SLOP) {
        gSelected = Idx;
        gClickSel = -1;
        OpenSelected();
        return;
    }

    gSelected = Idx;
    gClickSel = Idx;
    gClickClock = Now;
    gClickX = X;
    gClickY = Y;
    PaintList();
}

void FilesUiOnEscape(void) {
    if (!FilesUiIsFocused()) {
        return;
    }
    if (gMode == FILES_MODE_VIEW) {
        gMode = FILES_MODE_LIST;
        Paint();
        return;
    }
    if (gCwd[0]) {
        CwdPop();
        (void)ReloadList();
        Paint();
    }
}

void FilesUiOnEnter(void) {
    if (!FilesUiIsFocused() || gMode != FILES_MODE_LIST) {
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
    PaintList();
}

int FilesUiIsFocused(void) {
    return GuiFocusKind() == GUI_WIN_FILES;
}
