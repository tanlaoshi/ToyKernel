/*
 * FilesUi.c — 文件浏览器（PR-FB1 只读 + PR-FB2 写操作）
 *
 * 列表：进目录 / 开 ELF / 预览文本
 * 写：d/Del 删除（Y/N 确认）；n 新建目录；f 新建空文件；r 重命名
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
#define FILES_NAME_MAX     48
#define FILES_DBLCLICK_MAX 2000000ULL
#define FILES_DBLCLICK_SLOP 16u

typedef enum {
    FILES_MODE_LIST = 0,
    FILES_MODE_VIEW,
    FILES_MODE_CONFIRM,
    FILES_MODE_PROMPT
} FILES_MODE;

typedef enum {
    FILES_PROMPT_MKDIR = 0,
    FILES_PROMPT_NEWFILE,
    FILES_PROMPT_RENAME
} FILES_PROMPT_KIND;

static char gCwd[FILES_PATH_MAX];
static FAT_DIR_ENT gEnts[FAT_LIST_MAX];
static int gCount;
static int gSelected;
static int gScroll;
static FILES_MODE gMode;
static char gView[FILES_VIEW_MAX];
static UINTN gViewLen;
static char gViewTitle[FAT_ENT_NAME_MAX];

static FILES_PROMPT_KIND gPromptKind;
static char gPrompt[FILES_NAME_MAX];
static int gPromptLen;
static char gStatus[80];

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

static void SetStatus(const char *S) {
    CopyStr(gStatus, sizeof(gStatus), S ? S : "");
}

static int NameOk(const char *N) {
    int i;
    if (!N || !N[0]) {
        return 0;
    }
    if (N[0] == '.' && (N[1] == 0 || (N[1] == '.' && N[2] == 0))) {
        return 0;
    }
    for (i = 0; N[i]; i++) {
        char C = N[i];
        int Ok = (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') ||
                 (C >= '0' && C <= '9') || C == '.' || C == '_' || C == '-';
        if (!Ok || i >= FILES_NAME_MAX - 1) {
            return 0;
        }
    }
    return 1;
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
        SetStatus(FatStrError(Err));
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

static void PaintOverlay(const char *Line1, const char *Line2, const char *Line3) {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Bg;
    UINT32 LineH;
    UINT32 BoxX;
    UINT32 BoxY;
    UINT32 BoxW;
    UINT32 BoxH;

    if (!GuiFocusClient(&X, &Y, &W, &H, &Bg)) {
        return;
    }
    LineH = FontAdvanceY();
    if (LineH < 16) {
        LineH = 16;
    }
    BoxW = W > 40 ? W - 40 : W;
    BoxH = LineH * 5 + 24;
    if (BoxH > H - 20) {
        BoxH = H > 20 ? H - 20 : H;
    }
    BoxX = X + (W - BoxW) / 2;
    BoxY = Y + (H - BoxH) / 2;

    GuiFrameBufferBegin();
    HalVideoFillRect(BoxX, BoxY, BoxW, BoxH, COLOR_LIGHT_GRAY);
    HalVideoFillRect(BoxX, BoxY, BoxW, 2, COLOR_BLACK);
    HalVideoFillRect(BoxX, BoxY + BoxH - 2, BoxW, 2, COLOR_BLACK);
    HalVideoFillRect(BoxX, BoxY, 2, BoxH, COLOR_BLACK);
    HalVideoFillRect(BoxX + BoxW - 2, BoxY, 2, BoxH, COLOR_BLACK);
    HalVideoSetClipRegion(BoxX + 4, BoxY + 4, BoxW > 8 ? BoxW - 8 : BoxW, BoxH > 8 ? BoxH - 8 : BoxH,
                          COLOR_LIGHT_GRAY);
    DrawLine(BoxX + 12, BoxY + 12, Line1 ? Line1 : "", COLOR_BLACK);
    if (Line2) {
        DrawLine(BoxX + 12, BoxY + 12 + LineH, Line2, COLOR_BLACK);
    }
    if (Line3) {
        DrawLine(BoxX + 12, BoxY + 12 + LineH * 2, Line3, COLOR_DARK_GRAY);
    }
    HalVideoClearClip();
    GuiBackupFocusWindow();
    GuiFrameBufferEnd();
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
             "Enter open  d/Del rm  n mkdir  f file  r rename", COLOR_DARK_GRAY);
    if (gStatus[0]) {
        DrawLine(X + 8, Y + 8 + LineH * 2, gStatus, COLOR_BLUE);
    }

    Visible = 0;
    if (H > 8 + LineH * 4) {
        Visible = (int)((H - 8 - LineH * 4) / LineH);
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

    RowY = Y + 8 + LineH * 3 + 4;
    for (i = 0; i < Visible && gScroll + i < gCount; i++) {
        const FAT_DIR_ENT *E = &gEnts[gScroll + i];
        int Idx = gScroll + i;
        UINT32 Fg = COLOR_BLACK;
        int k = 0;
        int j;

        if (Idx == gSelected) {
            HalVideoFillRect(X + 4, RowY, W > 8 ? W - 8 : W, LineH, COLOR_YELLOW);
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

static void PaintConfirm(void) {
    char Line2[80];
    const char *Name = "?";

    if (gSelected >= 0 && gSelected < gCount) {
        Name = gEnts[gSelected].Name;
    }
    CopyStr(Line2, sizeof(Line2), "Delete ");
    {
        int n = 0;
        while (Line2[n]) {
            n++;
        }
        CopyStr(Line2 + n, (int)sizeof(Line2) - n, Name);
        n = 0;
        while (Line2[n]) {
            n++;
        }
        CopyStr(Line2 + n, (int)sizeof(Line2) - n, " ?");
    }
    PaintList();
    PaintOverlay("Confirm delete", Line2, "Y = yes   N/Esc = cancel");
}

static void PaintPrompt(void) {
    char Title[40];
    char Line2[FILES_NAME_MAX + 8];
    int i;

    if (gPromptKind == FILES_PROMPT_MKDIR) {
        CopyStr(Title, sizeof(Title), "New directory");
    } else if (gPromptKind == FILES_PROMPT_NEWFILE) {
        CopyStr(Title, sizeof(Title), "New empty file");
    } else {
        CopyStr(Title, sizeof(Title), "Rename to");
    }
    Line2[0] = '>';
    Line2[1] = ' ';
    for (i = 0; i < gPromptLen && i < FILES_NAME_MAX - 1; i++) {
        Line2[2 + i] = gPrompt[i];
    }
    Line2[2 + i] = '_';
    Line2[3 + i] = 0;
    PaintList();
    PaintOverlay(Title, Line2, "Enter=ok  Esc=cancel  Backspace");
}

static void Paint(void) {
    if (!FocusFilesWindow()) {
        return;
    }
    if (gMode == FILES_MODE_VIEW) {
        PaintView();
    } else if (gMode == FILES_MODE_CONFIRM) {
        PaintConfirm();
    } else if (gMode == FILES_MODE_PROMPT) {
        PaintPrompt();
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
        SetStatus("");
        (void)ReloadList();
        Paint();
        return;
    }

    if (!JoinPath(Path, sizeof(Path), gCwd, E->Name)) {
        return;
    }
    if (EndsWithElf(E->Name)) {
        if (ProcessExec(Path) != 0) {
            SetStatus("exec failed");
            Paint();
        }
        return;
    }

    gViewLen = 0;
    if (FsReadFile(Path, gView, sizeof(gView) - 1, &gViewLen) != FAT_OK) {
        SetStatus("read failed");
        Paint();
        return;
    }
    gView[gViewLen] = 0;
    CopyStr(gViewTitle, sizeof(gViewTitle), E->Name);
    gMode = FILES_MODE_VIEW;
    Paint();
}

static void BeginConfirmDelete(void) {
    if (gMode != FILES_MODE_LIST || gSelected < 0 || gSelected >= gCount) {
        return;
    }
    if (gEnts[gSelected].Name[0] == '.' &&
        (gEnts[gSelected].Name[1] == 0 ||
         (gEnts[gSelected].Name[1] == '.' && gEnts[gSelected].Name[2] == 0))) {
        SetStatus("cannot delete . / ..");
        Paint();
        return;
    }
    gMode = FILES_MODE_CONFIRM;
    Paint();
}

static void BeginPrompt(FILES_PROMPT_KIND Kind) {
    if (gMode != FILES_MODE_LIST) {
        return;
    }
    if (Kind == FILES_PROMPT_RENAME) {
        if (gSelected < 0 || gSelected >= gCount) {
            return;
        }
        if (gEnts[gSelected].Name[0] == '.' &&
            (gEnts[gSelected].Name[1] == 0 ||
             (gEnts[gSelected].Name[1] == '.' && gEnts[gSelected].Name[2] == 0))) {
            SetStatus("cannot rename . / ..");
            Paint();
            return;
        }
    }
    gPromptKind = Kind;
    gPromptLen = 0;
    gPrompt[0] = 0;
    gMode = FILES_MODE_PROMPT;
    Paint();
}

static void DoDelete(void) {
    char Path[FILES_PATH_MAX];
    int Err;
    int IsDir;

    if (gSelected < 0 || gSelected >= gCount) {
        gMode = FILES_MODE_LIST;
        Paint();
        return;
    }
    IsDir = (gEnts[gSelected].Attr & FAT_ATTR_DIR) != 0;
    if (!JoinPath(Path, sizeof(Path), gCwd, gEnts[gSelected].Name)) {
        gMode = FILES_MODE_LIST;
        Paint();
        return;
    }
    Err = IsDir ? FsRmdir(Path) : FsDeleteFile(Path);
    gMode = FILES_MODE_LIST;
    if (Err != FAT_OK) {
        SetStatus(FatStrError(Err));
    } else {
        SetStatus("deleted");
    }
    (void)ReloadList();
    Paint();
}

static void DoPromptCommit(void) {
    char Path[FILES_PATH_MAX];
    char OldPath[FILES_PATH_MAX];
    int Err = FAT_OK;

    gPrompt[gPromptLen] = 0;
    if (!NameOk(gPrompt)) {
        SetStatus("bad name");
        gMode = FILES_MODE_LIST;
        Paint();
        return;
    }
    if (!JoinPath(Path, sizeof(Path), gCwd, gPrompt)) {
        gMode = FILES_MODE_LIST;
        Paint();
        return;
    }

    if (gPromptKind == FILES_PROMPT_MKDIR) {
        Err = FsMkdir(Path);
        SetStatus(Err == FAT_OK ? "mkdir ok" : FatStrError(Err));
    } else if (gPromptKind == FILES_PROMPT_NEWFILE) {
        /* vvfat：0 字节文件常不落宿主盘，重开即消失；写 1 字节换行可持久化 */
        Err = FsWriteFile(Path, "\n", 1);
        SetStatus(Err == FAT_OK ? "file ok" : FatStrError(Err));
    } else {
        if (!JoinPath(OldPath, sizeof(OldPath), gCwd, gEnts[gSelected].Name)) {
            gMode = FILES_MODE_LIST;
            Paint();
            return;
        }
        Err = FsRename(OldPath, Path);
        SetStatus(Err == FAT_OK ? "renamed" : FatStrError(Err));
    }
    gMode = FILES_MODE_LIST;
    (void)ReloadList();
    Paint();
}

void FilesUiOpen(void) {
    gCwd[0] = 0;
    gMode = FILES_MODE_LIST;
    gClickSel = -1;
    SetStatus("");
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
    } else if (gMode == FILES_MODE_CONFIRM) {
        PaintConfirm();
    } else if (gMode == FILES_MODE_PROMPT) {
        PaintPrompt();
    } else {
        PaintList();
    }
}

void FilesUiRefresh(void) {
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

    if (gMode == FILES_MODE_VIEW || gMode == FILES_MODE_CONFIRM ||
        gMode == FILES_MODE_PROMPT) {
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
    ListTop = Cy + 8 + LineH * 3 + 4;
    if (Y < ListTop) {
        return;
    }
    Visible = 0;
    if (Ch > 8 + LineH * 4) {
        Visible = (int)((Ch - 8 - LineH * 4) / LineH);
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

int FilesUiIsFocused(void) {
    return GuiFocusKind() == GUI_WIN_FILES;
}
