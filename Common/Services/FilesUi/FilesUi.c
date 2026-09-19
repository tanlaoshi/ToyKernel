/*
 * FilesUi.c — 文件浏览器（PR-FB1/FB2 + PR-U1/U2/U3）
 *
 * 列表：进目录 / 开 ELF / 预览文本
 * 写：d/Del 删除（Y/N 确认）；n 新建目录；f 新建空文件；r 重命名
 * U1：左栏固定宽 + 右栏列表
 * U2：侧栏按已挂载卷列出（TOYOS 置顶；有 TOYOS 时附 Apps/Assets）；跨盘标 drive
 * 开窗一次填满客户区后再淡入（勿空框 Present）
 * U3：右栏再分 列表 | 预览；空态/焦点行与 G12 一致
 * PR-S-filesui-split-1：Paint* → FilesUiPaint.c；本文件为全局宿主。
 */
#include "FilesUiPriv.h"

char gCwd[FILES_PATH_MAX];
FAT_DIRECTORY_ENTRY gEnts[FAT_LIST_MAX];
int gCount;
int gSelected;
int gScroll;
FILES_MODE gMode;
char gView[FILES_VIEW_MAX];
UINTN gViewLen;
char gViewTitle[FAT_ENT_NAME_MAX];

FILES_PROMPT_KIND gPromptKind;
char gPrompt[FILES_NAME_MAX];
int gPromptLen;
char gStatus[80];

int gClickSel = -1;
UINT64 gClickClock;
UINT32 gClickX;
UINT32 gClickY;
int gHoverIdx = -1;
int gSideHover = -1;
int gSideSel = -1;

/* 侧栏：RebuildPlaces() 填 Label/Path 缓冲 */
char gPlaceLabels[FILES_PLACE_MAX][FILES_PLACE_LABEL_MAX];
char gPlacePaths[FILES_PLACE_MAX][FILES_PLACE_PATH_MAX];
FILES_PLACE gPlaces[FILES_PLACE_MAX];
int gPlaceCount;

UINT32 gSbX;
UINT32 gSbY;
UINT32 gSbW;
UINT32 gSbH;
int gSbVisible;
int gListVisible;
UINT32 gListTop;
UINT32 gListRowW;
UINT32 gListLineH;
UINT32 gContentX;
UINT32 gContentW;
UINT32 gSideX;
UINT32 gSideY;
UINT32 gSideW;
UINT32 gSideRow0;
UINT32 gSideLineH;
UINT32 gPrevX;
UINT32 gPrevW;

PREV_KIND gPrevKind;

UINT64 FilesClock(void) {
    return HalCpuTicks(0);
}

int FocusFilesWindow(void) {
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

int FilesUiStrEqIgnoreCase(const char *A, const char *B) {
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

int EndsWithElf(const char *Name) {
    int N = 0;
    while (Name[N]) {
        N++;
    }
    if (N < 4) {
        return 0;
    }
    return FilesUiStrEqIgnoreCase(Name + N - 4, ".elf");
}

void CopyStr(char *Dst, int Max, const char *Src) {
    int i;
    if (Max <= 0) {
        return;
    }
    for (i = 0; Src && Src[i] && i < Max - 1; i++) {
        Dst[i] = Src[i];
    }
    Dst[i] = 0;
}

void SetStatus(const char *S) {
    CopyStr(gStatus, sizeof(gStatus), S ? S : "");
}

int NameOk(const char *N) {
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

int JoinPath(char *Out, int Max, const char *Dir, const char *Name) {
    int i = 0;
    int j;

    if (!Out || Max <= 0 || !Name) {
        return 0;
    }
    if (Dir && Dir[0]) {
        for (j = 0; Dir[j] && i < Max - 1; j++) {
            Out[i++] = Dir[j];
        }
        if (i < Max - 1 && i > 0 && Out[i - 1] != '/' && Out[i - 1] != ':') {
            Out[i++] = '/';
        }
    }
    for (j = 0; Name[j] && i < Max - 1; j++) {
        Out[i++] = Name[j];
    }
    Out[i] = 0;
    return i > 0;
}

void CwdPop(void) {
    int i;
    int Last = -1;
    int Colon = -1;

    for (i = 0; gCwd[i]; i++) {
        if (gCwd[i] == ':') {
            Colon = i;
        }
        if (gCwd[i] == '/') {
            Last = i;
        }
    }
    /* 有子路径：退一层 */
    if (Last > Colon) {
        gCwd[Last] = 0;
        return;
    }
    /* 卷根保留 "TOYOS:"；无前缀则清空 */
    if (Colon >= 0) {
        gCwd[Colon + 1] = 0;
        return;
    }
    gCwd[0] = 0;
}

int PathEqIgnoreCase(const char *A, const char *B) {
    return FilesUiStrEqIgnoreCase(A ? A : "", B ? B : "");
}

void FilesUiOpen(void) {
    int i;
    int ToyPlace = -1;

    /*
     * 开窗一次填满：侧栏 + 目录 List 都在淡入前完成，避免空框闪一下再刷内容。
     * 侧栏 TOYOS 置顶；开窗优先进 TOYOS:。
     */
    RebuildPlaces();
    gCwd[0] = 0;
    for (i = 0; i < gPlaceCount; i++) {
        const char *P = gPlaces[i].Path;
        if (P && (P[0] == 'T' || P[0] == 't') && (P[1] == 'O' || P[1] == 'o') &&
            (P[2] == 'Y' || P[2] == 'y') && (P[3] == 'O' || P[3] == 'o') &&
            (P[4] == 'S' || P[4] == 's') && P[5] == ':') {
            ToyPlace = i;
            break;
        }
    }
    if (ToyPlace >= 0) {
        CopyStr(gCwd, sizeof(gCwd), gPlaces[ToyPlace].Path);
        SetStatus("");
    } else if (gPlaceCount > 0 && gPlaces[0].Path) {
        CopyStr(gCwd, sizeof(gCwd), gPlaces[0].Path);
        SetStatus("No TOYOS volume (vols / msc mount)");
    } else {
        SetStatus("No volumes mounted");
    }
    gMode = FILES_MODE_LIST;
    gCount = 0;
    gSelected = 0;
    gScroll = 0;
    gClickSel = -1;
    gHoverIdx = -1;
    gSideHover = -1;
    gPrevKind = PREV_EMPTY;
    gViewLen = 0;
    gViewTitle[0] = 0;
    SyncSideSel();
    /* 目录在首次 Paint 前就绪；跳过文件内容预览 */
    (void)ReloadListEx(0, 0);
    Paint();
}

void FilesUiFinishOpen(void) {
    /* 兼容旧调用点：内容已在 FilesUiOpen 填完 */
    if (GuiFocusKind() != GUI_WIN_FILES) {
        return;
    }
    if (gCount <= 0) {
        (void)ReloadListEx(0, 0);
        Paint();
    }
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

int FilesUiIsFocused(void) {
    return GuiFocusKind() == GUI_WIN_FILES;
}
