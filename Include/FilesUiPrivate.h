/*
 * FilesUiPrivate.h — FilesUi 内部共享头（仅 Common/Services 下 FilesUi*.c 使用）
 *
 * 禁止 User 程序、HAL、Common/Core 包含本文件。
 * PR-S-filesui-split-1：Paint 迁出；全局定义仍在 FilesUi.c。
 * 源文件在 Common/Services/FilesUi/（核心 FilesUi.c）。
 */
#ifndef FILES_UI_PRIVATE_H
#define FILES_UI_PRIVATE_H

#include "FilesUi.h"
#include "Gui.h"
#include "FileSystem.h"
#include "Process.h"
#include "Theme.h"
#include "Font.h"
#include "UI.h"
#include "Hal.h"
#include "Debug.h"
#include "Locale.h"

/* ===== 宏（从 FilesUi.c 搬入；值不变） ===== */
#define FILES_PATH_MAX      96
#define FILES_VIEW_MAX      2048
#define FILES_NAME_MAX      48
#define FILES_DBLCLICK_MAX  2000000ULL
#define FILES_DBLCLICK_SLOP 16u
#define FILES_SB_W          12u
#define FILES_SIDE_W        128u
#define FILES_SIDE_BG       0x00A0A8B0u
/* 侧栏 = 已挂载卷（动态）+ 可选 Apps/Assets；TOYOS 置顶 */
#define FILES_PLACE_MAX        (FS_MAX_VOLUMES + 2)
#define FILES_PLACE_LABEL_MAX  20
#define FILES_PLACE_PATH_MAX   16

/* ===== 类型（布局不变） ===== */
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

typedef enum {
    PREV_NONE = 0,
    PREV_EMPTY,
    PREV_DIR,
    PREV_ELF,
    PREV_TEXT,
    PREV_BIN,
    PREV_ERR
} PREV_KIND;

typedef struct {
    const char *Label;
    const char *Path;
} FILES_PLACE;

/* ===== 全局（定义在 FilesUi.c） ===== */
extern char gCwd[FILES_PATH_MAX];
extern FAT_DIRECTORY_ENTRY gEnts[FAT_LIST_MAX];
extern int gCount;
extern int gSelected;
extern int gScroll;
extern FILES_MODE gMode;
extern char gView[FILES_VIEW_MAX];
extern UINTN gViewLen;
extern char gViewTitle[FAT_ENT_NAME_MAX];

extern FILES_PROMPT_KIND gPromptKind;
extern char gPrompt[FILES_NAME_MAX];
extern int gPromptLen;
extern char gStatus[80];

extern int gClickSel;
extern UINT64 gClickClock;
extern UINT32 gClickX;
extern UINT32 gClickY;
extern int gHoverIdx;
extern int gSideHover;
extern int gSideSel;

extern FILES_PLACE gPlaces[FILES_PLACE_MAX];
extern int gPlaceCount;
extern char gPlaceLabels[FILES_PLACE_MAX][FILES_PLACE_LABEL_MAX];
extern char gPlacePaths[FILES_PLACE_MAX][FILES_PLACE_PATH_MAX];

extern UINT32 gSbX;
extern UINT32 gSbY;
extern UINT32 gSbW;
extern UINT32 gSbH;
extern int gSbVisible;
extern int gListVisible;
extern UINT32 gListTop;
extern UINT32 gListRowW;
extern UINT32 gListLineH;
extern UINT32 gContentX;
extern UINT32 gContentW;
extern UINT32 gSideX;
extern UINT32 gSideY;
extern UINT32 gSideW;
extern UINT32 gSideRow0;
extern UINT32 gSideLineH;
extern UINT32 gPrevX;
extern UINT32 gPrevW;
extern PREV_KIND gPrevKind;

/* ===== 工具（FilesUi.c） ===== */
UINT64 FilesClock(void);
int FocusFilesWindow(void);
int FilesUiStrEqIgnoreCase(const char *A, const char *B);
int EndsWithElf(const char *Name);
void CopyStr(char *Dst, int Max, const char *Src);
void SetStatus(const char *S);
int NameOk(const char *N);
int JoinPath(char *Out, int Max, const char *Dir, const char *Name);
void CwdPop(void);
int PathEqIgnoreCase(const char *A, const char *B);
void DrawLine(UINT32 X, UINT32 Y, const char *S, UINT32 Fg);

/* ===== Nav（仍在 FilesUi.c，待 split-2） ===== */
void RebuildPlaces(void);
int PlaceMatches(int Idx);
void SyncSideSel(void);
void GotoPath(const char *Path);
int SideHitIndex(UINT32 X, UINT32 Y);
/* WantPlaces=0：跳过卷枚举；WantPreview=0：不读文件内容（开窗加速） */
int ReloadListEx(int WantPlaces, int WantPreview);
int ReloadList(void);
int IsMostlyText(const char *Buf, UINTN Len);
void UpdatePreview(void);

/* ===== Paint（FilesUiPaint.c） ===== */
void PaintOverlay(const char *Line1, const char *Line2, const char *Line3);
void PaintList(void);
void PaintView(void);
void PaintConfirm(void);
void PaintPrompt(void);
void Paint(void);

/* ===== Actions（仍在 FilesUi.c，待 split-3） ===== */
void OpenSelected(void);
void BeginConfirmDelete(void);
void BeginPrompt(FILES_PROMPT_KIND Kind);
void DoDelete(void);
void DoPromptCommit(void);

#endif
