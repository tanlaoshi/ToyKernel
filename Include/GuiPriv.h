/*
 * GuiPriv.h — PR-R2：Gui 子模块共享状态与内部 API（非对外）
 */
#ifndef GUI_PRIV_H
#define GUI_PRIV_H

#include "Gui.h"
#include "BootTypes.h"

#define DRAG_MIN_STEP 3
#define DRAG_ROW_MAX  1920
#define DRAG_BORDER_PAD 2

#define MAX_WINS     GUI_MAX_WINS
#define TITLE_HEIGHT GUI_TITLE_HEIGHT
#define CLOSE_SIZE   24
#define CLOSE_MARGIN 6
#define CURSOR_HALF  6
#define CURSOR_BOX   (CURSOR_HALF * 2 + 1)

typedef struct {
    int      Active;
    GUI_WIN_KIND Kind;
    UINT32   X;
    UINT32   Y;
    UINT32   Width;
    UINT32   Height;
    UINT32   Background;
    const char *Title;
    UINT32   TermX;
    UINT32   TermY;
    int      TermSet;
    char     InputLine[GUI_INPUT_LINE_MAX];
    int      InputLen;
    int      WaitPrompt;
    int      PromptShown;
    char     TitleBuf[64];
    char     ClientText[128];
    int      ClosePending;
    int      UserButtonUsed[4];
    char     UserButtonLabel[4][24];
    int      UserButtonClick;
} GUI_WINDOW;

/* 共享状态（定义见各 .c） */
extern GUI_WINDOW gWindows[MAX_WINS];
extern UINT32 gScreenWidth;
extern UINT32 gScreenHeight;
extern UINT32 gCursorX;
extern UINT32 gCursorY;
extern UINT8  gCursorBtn;
extern int    gFocusWin;

extern UINT32 gSaveX;
extern UINT32 gSaveY;
extern UINT32 gSaveW;
extern UINT32 gSaveH;
extern UINT32 gUnder[CURSOR_BOX * CURSOR_BOX];
extern int    gCursorVisible;

extern int    gDragWin;
extern INT32  gDragOffX;
extern INT32  gDragOffY;
extern int    gDragArmed;
extern GUI_WINDOW gWinSwap;

extern int      gDragHasBackup;
extern int      gWinBackupValid[MAX_WINS];
extern UINT32   gWinBackupW[MAX_WINS];
extern UINT32   gWinBackupH[MAX_WINS];
extern UINT32   gWinBackupPages[MAX_WINS];
extern UINT32  *gWinBackup[MAX_WINS];
extern UINT32   gDragRowBuf[DRAG_ROW_MAX];
extern UINT32  *gDragDirty;
extern UINT32   gDragDirtyPages;
extern UINT32   gDragDirtyCap;

extern UINT32  *gScreenSnap;
extern UINT32   gScreenSnapPages;
extern int      gScreenSnapValid;
extern UINT32  *gUnderDrag;
extern UINT32   gUnderDragPages;
extern int      gUnderDragValid;
extern UINT32   gDragStartX;
extern UINT32   gDragStartY;
extern UINT32   gDragStartW;
extern UINT32   gDragStartH;

extern int    gGfxLockDepth;
extern UINT64 gGfxIrqFlags;
extern int    gComposeBusy;
extern int    gDeferPresent;

extern GUI_CONSOLE_OPS gGuiConsoleOps;

static inline void GuiConsoleOpsFocusSave(void) {
    if (gGuiConsoleOps.FocusSave) {
        gGuiConsoleOps.FocusSave();
    }
}
static inline void GuiConsoleOpsFocusLoad(void) {
    if (gGuiConsoleOps.FocusLoad) {
        gGuiConsoleOps.FocusLoad();
    }
}
static inline void GuiConsoleOpsOnShellOpened(void) {
    if (gGuiConsoleOps.OnShellOpened) {
        gGuiConsoleOps.OnShellOpened();
    }
}
static inline void GuiConsoleOpsPaintShellWindow(int Idx) {
    if (gGuiConsoleOps.PaintShellWindow) {
        gGuiConsoleOps.PaintShellWindow(Idx);
    }
}

/* Compose / gfx */
void GfxIrqEnter(void);
void GfxIrqLeave(void);
void ComposeBegin(void);
void ComposeEnd(void);
void GfxPresent(void);
UINT32 TitleBarColor(int Idx);
void CloseButtonRect(const GUI_WINDOW *W, UINT32 *Bx, UINT32 *By, UINT32 *Bw, UINT32 *Bh);
void DrawCloseButton(int Idx, const GUI_WINDOW *W);
int PixelOccludedByAbove(int Idx, UINT32 X, UINT32 Y);
void FillRectOccluded(int Idx, UINT32 X, UINT32 Y, UINT32 W, UINT32 H, UINT32 Color);
void DrawHLineOccluded(int Idx, UINT32 X0, UINT32 X1, UINT32 Y, UINT32 Color);
void DrawVLineOccluded(int Idx, UINT32 X, UINT32 Y0, UINT32 Y1, UINT32 Color);
void DrawTitleStringOccluded(int Idx, const GUI_WINDOW *W);
void DrawWindowAtEx(int Idx, int Occlude);
void DrawWindowAt(int Idx);
void DrawWindowChromeAt(int Idx);
void RefreshOtherChrome(int SkipIdx);
void SyncWindowVisualsEx(int ClearDesktop);
void SyncWindowVisuals(void);
void FillDesktopRectClipped(UINT32 X, UINT32 Y, UINT32 W, UINT32 H);
int WindowOccludedByOther(int Idx);
int AllActiveWindowsHaveValidBackup(void);
UINT32 BackupPageCount(UINT32 Ww, UINT32 Wh);
int EnsureWindowBackupBuf(int Idx);
void PreallocWindowBackups(void);
void BackupWindowAtEx(int Idx, int ForceFull);
void BackupWindowAt(int Idx);
UINT32 AnalyticWindowPixel(int Idx, UINT32 Px, UINT32 Py);
void PaintWindowFromBackup(int Idx);
void ShiftWinBackupsUp(int From, int To);
int RectIntersects(UINT32 Ax, UINT32 Ay, UINT32 Aw, UINT32 Ah,
                   UINT32 Bx, UINT32 By, UINT32 Bw, UINT32 Bh);
void ClipRectToScreen(UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H);
UINT32 SampleWindowBackupPixel(int Idx, UINT32 Px, UINT32 Py);
int WindowBackupCoversPixel(int Idx, UINT32 Px, UINT32 Py);
int PixelCoveredByHigherWindow(int Idx, UINT32 Px, UINT32 Py);

/* Cursor */
void CursorBox(UINT32 Cx, UINT32 Cy, UINT32 *Sx, UINT32 *Sy, UINT32 *Sw, UINT32 *Sh);
void DrawCursorAt(UINT32 X, UINT32 Y);
void CursorRestore(void);
void CursorPaint(void);
void CursorMove(UINT32 X, UINT32 Y);

/* Drag */
void ResetDragState(void);
int AnyWindowsOverlap(void);
UINT32 TopmostBelowDragPixel(UINT32 Px, UINT32 Py, int DragIdx);
int EnsureDragDirtyBuf(UINT32 Ww, UINT32 Hh);
int EnsureUnderDragBuf(UINT32 Ww, UINT32 Wh);
void CaptureDragRestoreData(int DragIdx);
void BeginDragBackups(int DragIdx);
void StartDragBackups(int DragIdx);
UINT32 CompositeDragPixel(UINT32 Px, UINT32 Py, int DragIdx,
                          UINT32 Nx, UINT32 Ny, UINT32 Nw, UINT32 Nh);
void CompositeDragDirtyRegion(int DragIdx, UINT32 OldX, UINT32 OldY,
                              UINT32 Ww, UINT32 Wh);
void RestoreWindowsInFootprint(UINT32 Fx, UINT32 Fy, UINT32 Fw, UINT32 Fh, int SkipIdx);
void ClearOldDragFootprint(UINT32 Ox, UINT32 Oy, UINT32 Ww, UINT32 Wh, int DragIdx);
void PaintAllWindowsDraw(int DragIdx);
void RedrawDragFrame(int DragIdx, UINT32 OldX, UINT32 OldY);
void ClampWindowPos(const GUI_WINDOW *W, INT32 *X, INT32 *Y);
void MoveWindowTo(int Idx, UINT32 NewX, UINT32 NewY);
void GuiDragUpdate(UINT32 X, UINT32 Y);
void GuiDragEnd(void);

/* Wm helpers */
void WinCopy(GUI_WINDOW *Dst, const GUI_WINDOW *Src);
int PointInClose(const GUI_WINDOW *W, UINT32 X, UINT32 Y);
void CloseWindow(int Idx);
int PointInWindow(const GUI_WINDOW *W, UINT32 X, UINT32 Y);
int PointInTitle(const GUI_WINDOW *W, UINT32 X, UINT32 Y);
int PointInAnyActiveWindow(UINT32 X, UINT32 Y);
int PointOnAnyClose(UINT32 X, UINT32 Y);
void RaiseWindow(int Idx);
int AllocWindowSlot(void);
void PlaceNewWindow(int Idx, UINT32 *OutX, UINT32 *OutY, UINT32 *OutW, UINT32 *OutH);

/* User helpers */
void CopyTitleBuf(char *Dst, UINTN Cap, const char *Src);
void PaintUserClient(int Idx);
int UserWindowIndexAfterRaise(int Wid);
void RepaintUserWindow(int Wid);
int UserButtonHit(int Idx, UINT32 X, UINT32 Y);

#endif
