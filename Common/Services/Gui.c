/*
 * Gui.c — 桌面、窗口、光标绘制
 *
 * 光标：关中断 → 恢复旧像素 → 保存新位置 → 绘制十字。
 * 避免 XOR / 局部重绘在抢占下留下轨迹。
 */
#include "Gui.h"
#include "UI.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Debug.h"
#include "Console.h"
#include "PhysicalMemory.h"

#define DRAG_MIN_STEP 3
#define DRAG_ROW_MAX  1920
#define DRAG_BORDER_PAD 2

#define MAX_WINS     4
#define TITLE_HEIGHT GUI_TITLE_HEIGHT
#define CLOSE_SIZE   24
#define CLOSE_MARGIN 6
#define CURSOR_HALF  6
#define CURSOR_BOX   (CURSOR_HALF * 2 + 1)

typedef struct {
    int      Active;
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
} GUI_WINDOW;

static GUI_WINDOW gWins[MAX_WINS];
static UINT32 gScreenW;
static UINT32 gScreenH;
static UINT32 gCursorX;
static UINT32 gCursorY;
static UINT8  gCursorBtn;
static int    gFocusWin;

static UINT32 gSaveX;
static UINT32 gSaveY;
static UINT32 gSaveW;
static UINT32 gSaveH;
static UINT32 gUnder[CURSOR_BOX * CURSOR_BOX];
static int    gCursorVisible;

/* 标题栏拖动：按下时记录窗口下标与光标相对偏移 */
static int    gDragWin = -1;
static INT32  gDragOffX;
static INT32  gDragOffY;
static GUI_WINDOW gWinSwap;

/* PR-G4：拖动时整窗备份（GuiInit 预分配），每帧轨迹合成重画 */
static int      gDragHasBackup;
static int      gWinBackupValid[MAX_WINS];
static UINT32   gWinBackupW[MAX_WINS];
static UINT32   gWinBackupH[MAX_WINS];
static UINT32   gWinBackupPages[MAX_WINS];
static UINT32  *gWinBackup[MAX_WINS];
static UINT32   gDragRowBuf[DRAG_ROW_MAX];
/* 脏区离屏缓冲：整块一次 WriteRect，避免逐行露出造成闪屏 */
static UINT32  *gDragDirty;
static UINT32   gDragDirtyPages;
static UINT32   gDragDirtyCap; /* 像素数上限 */

/* 拖动开始时的全屏快照 + 被拖窗 footprint 下方干净层（避免窗备份混入拖窗像素） */
static UINT32  *gScreenSnap;
static UINT32   gScreenSnapPages;
static int      gScreenSnapValid;
static UINT32  *gUnderDrag;
static UINT32   gUnderDragPages;
static int      gUnderDragValid;
static UINT32   gDragStartX;
static UINT32   gDragStartY;
static UINT32   gDragStartW;
static UINT32   gDragStartH;

static void WinCopy(GUI_WINDOW *Dst, const GUI_WINDOW *Src) {
    const UINT8 *S = (const UINT8 *)Src;
    UINT8 *D = (UINT8 *)Dst;
    UINTN N = sizeof(GUI_WINDOW);
    while (N--) {
        *D++ = *S++;
    }
}

/* 保存进入绘制区前的 IF，避免在中断/异常里 ConsoleWrite 后误 sti 嵌套中断 */
static int    gGfxLockDepth;
static int    gGfxHadIrq;

static void GfxIrqEnter(void) {
    if (gGfxLockDepth++ == 0) {
        UINT64 Flags;
        __asm__ volatile ("pushfq; pop %0" : "=r"(Flags) :: "memory");
        gGfxHadIrq = (Flags & (1ULL << 9)) != 0;
        HalIrqDisable();
    }
}

static void GfxIrqLeave(void) {
    if (gGfxLockDepth > 0 && --gGfxLockDepth == 0 && gGfxHadIrq) {
        HalIrqEnable();
    }
}

static void DrawWindowChromeAt(int Idx);
static void DrawWindowAt(int Idx);
static void PaintAllWindowsFromBackup(int DragIdx);
static void RedrawWindowChrome(void);
static int RectIntersects(UINT32 Ax, UINT32 Ay, UINT32 Aw, UINT32 Ah,
                          UINT32 Bx, UINT32 By, UINT32 Bw, UINT32 Bh);
static void RefreshOtherChrome(int SkipIdx);
static void CursorRestore(void);
static void CursorPaint(void);
static void SyncWindowVisuals(void);
void GuiFocusApply(void);
void GuiFocusApplyClip(void);
void GuiFocusSyncCursor(void);
void GuiFocusClearClient(void);
int GuiShellAcceptsInput(void);
void GuiRedraw(void);

static UINT32 TitleBarColor(int Idx) {
    if (Idx == gFocusWin && gWins[Idx].Active) {
        return COLOR_BLUE;
    }
    return COLOR_GRAY;
}

static void CloseButtonRect(const GUI_WINDOW *W, UINT32 *Bx, UINT32 *By,
                            UINT32 *Bw, UINT32 *Bh) {
    *Bw = CLOSE_SIZE;
    *Bh = CLOSE_SIZE;
    *Bx = W->X + W->Width - *Bw - CLOSE_MARGIN;
    *By = W->Y + (TITLE_HEIGHT - *Bh) / 2;
}

static int PointInClose(const GUI_WINDOW *W, UINT32 X, UINT32 Y) {
    UINT32 Bx;
    UINT32 By;
    UINT32 Bw;
    UINT32 Bh;

    if (!W->Active) {
        return 0;
    }
    CloseButtonRect(W, &Bx, &By, &Bw, &Bh);
    return X >= Bx && X < Bx + Bw && Y >= By && Y < By + Bh;
}

/* 更高 z（数组下标更大）的窗口是否盖住该像素 */
static int PixelOccludedByAbove(int Idx, UINT32 X, UINT32 Y) {
    int j;

    for (j = Idx + 1; j < MAX_WINS; j++) {
        if (!gWins[j].Active) {
            continue;
        }
        if (X >= gWins[j].X && X < gWins[j].X + gWins[j].Width &&
            Y >= gWins[j].Y && Y < gWins[j].Y + gWins[j].Height) {
            return 1;
        }
    }
    return 0;
}

static void FillRectOccluded(int Idx, UINT32 X, UINT32 Y, UINT32 W, UINT32 H,
                             UINT32 Color) {
    UINT32 Row;
    UINT32 Col;
    UINT32 RunStart;
    int InRun;

    if (!W || !H) {
        return;
    }
    for (Row = 0; Row < H; Row++) {
        UINT32 Py = Y + Row;

        InRun = 0;
        RunStart = 0;
        for (Col = 0; Col < W; Col++) {
            UINT32 Px = X + Col;
            int Occ = PixelOccludedByAbove(Idx, Px, Py);

            if (!Occ && !InRun) {
                RunStart = Col;
                InRun = 1;
            } else if (Occ && InRun) {
                HalVideoFillRect(X + RunStart, Py, Col - RunStart, 1, Color);
                InRun = 0;
            }
        }
        if (InRun) {
            HalVideoFillRect(X + RunStart, Py, W - RunStart, 1, Color);
        }
    }
}

static void DrawHLineOccluded(int Idx, UINT32 X0, UINT32 X1, UINT32 Y,
                              UINT32 Color) {
    UINT32 X;
    UINT32 RunStart = 0;
    int InRun = 0;

    if (X1 < X0 || Y >= gScreenH) {
        return;
    }
    for (X = X0; X <= X1; X++) {
        int Occ = PixelOccludedByAbove(Idx, X, Y);

        if (!Occ && !InRun) {
            RunStart = X;
            InRun = 1;
        } else if (Occ && InRun) {
            HalVideoFillRect(RunStart, Y, X - RunStart, 1, Color);
            InRun = 0;
        }
    }
    if (InRun) {
        HalVideoFillRect(RunStart, Y, X1 - RunStart + 1, 1, Color);
    }
}

static void DrawVLineOccluded(int Idx, UINT32 X, UINT32 Y0, UINT32 Y1,
                              UINT32 Color) {
    UINT32 Y;
    UINT32 RunStart = 0;
    int InRun = 0;

    if (Y1 < Y0 || X >= gScreenW) {
        return;
    }
    for (Y = Y0; Y <= Y1; Y++) {
        int Occ = PixelOccludedByAbove(Idx, X, Y);

        if (!Occ && !InRun) {
            RunStart = Y;
            InRun = 1;
        } else if (Occ && InRun) {
            HalVideoFillRect(X, RunStart, 1, Y - RunStart, Color);
            InRun = 0;
        }
    }
    if (InRun) {
        HalVideoFillRect(X, RunStart, 1, Y1 - RunStart + 1, Color);
    }
}

static void DrawCloseButton(int Idx, const GUI_WINDOW *W) {
    UINT32 Bx;
    UINT32 By;
    UINT32 Bw;
    UINT32 Bh;
    UINT32 Pad;
    UINT32 I;
    UINT32 Span;

    CloseButtonRect(W, &Bx, &By, &Bw, &Bh);
    FillRectOccluded(Idx, Bx, By, Bw, Bh, COLOR_RED);
    if (Bw >= 2 && Bh >= 2) {
        DrawHLineOccluded(Idx, Bx, Bx + Bw - 1, By, COLOR_WHITE);
        DrawHLineOccluded(Idx, Bx, Bx + Bw - 1, By + Bh - 1, COLOR_WHITE);
        DrawVLineOccluded(Idx, Bx, By, By + Bh - 1, COLOR_WHITE);
        DrawVLineOccluded(Idx, Bx + Bw - 1, By, By + Bh - 1, COLOR_WHITE);
    }
    /* 字体为 16×32，24×24 按钮内放不下；用对角线画居中 × */
    Pad = 7;
    if (Bw > Pad * 2 + 2 && Bh > Pad * 2 + 2) {
        Span = Bw - 1 - Pad * 2;
        for (I = 0; I <= Span; I++) {
            UINT32 PxA = Bx + Pad + I;
            UINT32 PyA = By + Pad + I;
            UINT32 PxB = Bx + Bw - 1 - Pad - I;
            UINT32 PyB = By + Pad + I;

            if (!PixelOccludedByAbove(Idx, PxA, PyA)) {
                HalVideoDrawPixel(PxA, PyA, COLOR_WHITE);
            }
            if (!PixelOccludedByAbove(Idx, PxB, PyB)) {
                HalVideoDrawPixel(PxB, PyB, COLOR_WHITE);
            }
        }
    }
}

static void RefreshOtherChrome(int SkipIdx) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (gWins[i].Active && i != SkipIdx) {
            DrawWindowChromeAt(i);
        }
    }
}

static void SyncWindowVisuals(void) {
    RedrawWindowChrome();
}

static void CloseWindow(int Idx) {
    UINT32 X;
    UINT32 Y;
    UINT32 Ww;
    UINT32 Wh;
    int i;

    if (Idx < 0 || Idx >= MAX_WINS || !gWins[Idx].Active) {
        return;
    }
    X = gWins[Idx].X;
    Y = gWins[Idx].Y;
    Ww = gWins[Idx].Width;
    Wh = gWins[Idx].Height;
    gWins[Idx].Active = 0;
    gWins[Idx].TermSet = 0;
    gWins[Idx].InputLen = 0;
    gWins[Idx].WaitPrompt = 0;
    gWins[Idx].PromptShown = 0;
    gWins[Idx].InputLine[0] = 0;
    if (gDragWin == Idx) {
        gDragWin = -1;
    }
    if (gFocusWin == Idx) {
        gFocusWin = -1;
        for (i = MAX_WINS - 1; i >= 0; i--) {
            if (gWins[i].Active) {
                gFocusWin = i;
                break;
            }
        }
    }
    HalIrqDisable();
    CursorRestore();
    UiFillRectangle(X, Y, Ww, Wh, COLOR_DARK_GRAY);
    for (i = 0; i < MAX_WINS; i++) {
        if (gWins[i].Active) {
            DrawWindowChromeAt(i);
        }
    }
    CursorPaint();
    HalIrqEnable();
    GuiFocusApply();
    DebugWrite("gui: closed window\n");
}

static void CursorBox(UINT32 Cx, UINT32 Cy, UINT32 *Sx, UINT32 *Sy,
                      UINT32 *Sw, UINT32 *Sh) {
    *Sx = Cx >= CURSOR_HALF ? Cx - CURSOR_HALF : 0;
    *Sy = Cy >= CURSOR_HALF ? Cy - CURSOR_HALF : 0;
    UINT32 Ex = Cx + CURSOR_HALF + 1;
    UINT32 Ey = Cy + CURSOR_HALF + 1;
    if (Ex > gScreenW) {
        Ex = gScreenW;
    }
    if (Ey > gScreenH) {
        Ey = gScreenH;
    }
    *Sw = Ex - *Sx;
    *Sh = Ey - *Sy;
}

static void DrawCursorAt(UINT32 X, UINT32 Y) {
    int i;

    for (i = -CURSOR_HALF; i <= CURSOR_HALF; i++) {
        int Px = (int)X + i;
        int Py = (int)Y + i;
        if (Px >= 0 && (UINT32)Px < gScreenW) {
            HalVideoDrawPixel((UINT32)Px, Y, COLOR_WHITE);
        }
        if (Py >= 0 && (UINT32)Py < gScreenH) {
            HalVideoDrawPixel(X, (UINT32)Py, COLOR_WHITE);
        }
    }
    HalVideoDrawPixel(X, Y, COLOR_RED);
}

static void CursorRestore(void) {
    UINT32 Dy;
    UINT32 Dx;

    if (!gCursorVisible) {
        return;
    }
    for (Dy = 0; Dy < gSaveH; Dy++) {
        for (Dx = 0; Dx < gSaveW; Dx++) {
            HalVideoDrawPixel(gSaveX + Dx, gSaveY + Dy,
                      gUnder[Dy * gSaveW + Dx]);
        }
    }
    gCursorVisible = 0;
}

static void CursorPaint(void) {
    UINT32 Dy;
    UINT32 Dx;

    CursorBox(gCursorX, gCursorY, &gSaveX, &gSaveY, &gSaveW, &gSaveH);
    for (Dy = 0; Dy < gSaveH; Dy++) {
        for (Dx = 0; Dx < gSaveW; Dx++) {
            gUnder[Dy * gSaveW + Dx] =
                HalVideoReadPixel(gSaveX + Dx, gSaveY + Dy);
        }
    }
    DrawCursorAt(gCursorX, gCursorY);
    gCursorVisible = 1;
}

static void CursorMove(UINT32 X, UINT32 Y) {
    if (X >= gScreenW) {
        X = gScreenW > 0 ? gScreenW - 1 : 0;
    }
    if (Y >= gScreenH) {
        Y = gScreenH > 0 ? gScreenH - 1 : 0;
    }
    if (X == gCursorX && Y == gCursorY) {
        return;
    }

    /* 拖动时只跟踪坐标；若光标仍可见则先擦掉，避免十字残影 */
    if (gDragWin >= 0) {
        if (gCursorVisible) {
            HalIrqDisable();
            CursorRestore();
            HalIrqEnable();
        }
        gCursorX = X;
        gCursorY = Y;
        return;
    }

    HalIrqDisable();
    CursorRestore();
    gCursorX = X;
    gCursorY = Y;
    CursorPaint();
    HalIrqEnable();
}

static void DrawWindowAt(int Idx) {
    const GUI_WINDOW *W = &gWins[Idx];

    if (!W->Active) {
        return;
    }
    FillRectOccluded(Idx, W->X, W->Y, W->Width, TITLE_HEIGHT, TitleBarColor(Idx));
    DrawHLineOccluded(Idx, W->X, W->X + W->Width - 1, W->Y, COLOR_WHITE);
    DrawHLineOccluded(Idx, W->X, W->X + W->Width - 1, W->Y + W->Height - 1,
                      COLOR_WHITE);
    DrawVLineOccluded(Idx, W->X, W->Y, W->Y + W->Height - 1, COLOR_WHITE);
    DrawVLineOccluded(Idx, W->X + W->Width - 1, W->Y, W->Y + W->Height - 1,
                      COLOR_WHITE);
    FillRectOccluded(Idx, W->X + 2, W->Y + TITLE_HEIGHT, W->Width - 4,
                     W->Height - TITLE_HEIGHT - 2, W->Background);
    HalVideoDrawStringAt(W->X + 8, W->Y + 4, W->Title, COLOR_WHITE);
    DrawCloseButton(Idx, W);
}

/* 仅重绘标题栏与边框，保留客户区已有文字；不画到上层窗口上 */
static void DrawWindowChromeAt(int Idx) {
    const GUI_WINDOW *W = &gWins[Idx];

    if (!W->Active) {
        return;
    }
    FillRectOccluded(Idx, W->X, W->Y, W->Width, TITLE_HEIGHT, TitleBarColor(Idx));
    DrawHLineOccluded(Idx, W->X, W->X + W->Width - 1, W->Y, COLOR_WHITE);
    DrawHLineOccluded(Idx, W->X, W->X + W->Width - 1, W->Y + W->Height - 1,
                      COLOR_WHITE);
    DrawVLineOccluded(Idx, W->X, W->Y, W->Y + W->Height - 1, COLOR_WHITE);
    DrawVLineOccluded(Idx, W->X + W->Width - 1, W->Y, W->Y + W->Height - 1,
                      COLOR_WHITE);
    /* 标题文字无逐像素遮挡；起点被盖住则跳过，避免写穿上层客户区 */
    if (!PixelOccludedByAbove(Idx, W->X + 8, W->Y + 4)) {
        HalVideoDrawStringAt(W->X + 8, W->Y + 4, W->Title, COLOR_WHITE);
    }
    DrawCloseButton(Idx, W);
}

static void RedrawWindowChrome(void) {
    int i;
    int Last = gFocusWin;

    HalIrqDisable();
    CursorRestore();
    for (i = 0; i < MAX_WINS; i++) {
        if (gWins[i].Active && i != Last) {
            DrawWindowChromeAt(i);
        }
    }
    if (Last >= 0 && Last < MAX_WINS && gWins[Last].Active) {
        DrawWindowChromeAt(Last);
    }
    CursorPaint();
    HalIrqEnable();
}

static int PointInWindow(const GUI_WINDOW *W, UINT32 X, UINT32 Y) {
    return W->Active && X >= W->X && X < W->X + W->Width &&
           Y >= W->Y && Y < W->Y + W->Height;
}

static int PointInTitle(const GUI_WINDOW *W, UINT32 X, UINT32 Y) {
    return W->Active && X >= W->X && X < W->X + W->Width &&
           Y >= W->Y && Y < W->Y + TITLE_HEIGHT;
}

static int PointInAnyActiveWindow(UINT32 X, UINT32 Y) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (gWins[i].Active && PointInWindow(&gWins[i], X, Y)) {
            return 1;
        }
    }
    return 0;
}

static void FillDesktopRectClipped(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    UINT32 Row;
    UINT32 Col;
    UINT32 RunStart;
    int InRun;

    if (!W || !H) {
        return;
    }
    for (Row = Y; Row < Y + H; Row++) {
        InRun = 0;
        RunStart = 0;
        for (Col = X; Col < X + W; Col++) {
            int Cover = PointInAnyActiveWindow(Col, Row);
            if (!Cover && !InRun) {
                RunStart = Col;
                InRun = 1;
            } else if (Cover && InRun) {
                if (Col > RunStart) {
                    HalVideoFillRect(RunStart, Row, Col - RunStart, 1,
                                  COLOR_DARK_GRAY);
                }
                InRun = 0;
            }
        }
        if (InRun && X + W > RunStart) {
            HalVideoFillRect(RunStart, Row, X + W - RunStart, 1, COLOR_DARK_GRAY);
        }
    }
}

static void ResetDragState(void) {
    int i;

    if (gScreenSnap != 0) {
        PhysicalMemoryFreePages(gScreenSnap, gScreenSnapPages);
        gScreenSnap = 0;
        gScreenSnapPages = 0;
    }
    if (gUnderDrag != 0) {
        PhysicalMemoryFreePages(gUnderDrag, gUnderDragPages);
        gUnderDrag = 0;
        gUnderDragPages = 0;
    }
    if (gDragDirty != 0) {
        PhysicalMemoryFreePages(gDragDirty, gDragDirtyPages);
        gDragDirty = 0;
        gDragDirtyPages = 0;
        gDragDirtyCap = 0;
    }
    gScreenSnapValid = 0;
    gUnderDragValid = 0;
    gDragStartW = 0;
    gDragStartH = 0;
    for (i = 0; i < MAX_WINS; i++) {
        gWinBackupValid[i] = 0;
    }
    gDragHasBackup = 0;
}

static int AnyWindowsOverlap(void) {
    int i;
    int j;

    for (i = 0; i < MAX_WINS; i++) {
        if (!gWins[i].Active) {
            continue;
        }
        for (j = i + 1; j < MAX_WINS; j++) {
            if (!gWins[j].Active) {
                continue;
            }
            if (RectIntersects(gWins[i].X, gWins[i].Y, gWins[i].Width, gWins[i].Height,
                               gWins[j].X, gWins[j].Y, gWins[j].Width, gWins[j].Height)) {
                return 1;
            }
        }
    }
    return 0;
}

static int WindowOccludedByOther(int Idx) {
    int j;

    for (j = Idx + 1; j < MAX_WINS; j++) {
        if (!gWins[j].Active) {
            continue;
        }
        if (RectIntersects(gWins[Idx].X, gWins[Idx].Y, gWins[Idx].Width, gWins[Idx].Height,
                           gWins[j].X, gWins[j].Y, gWins[j].Width, gWins[j].Height)) {
            return 1;
        }
    }
    return 0;
}

static int AllActiveWindowsHaveValidBackup(void) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (gWins[i].Active && !gWinBackupValid[i]) {
            return 0;
        }
    }
    return 1;
}

static UINT32 BackupPageCount(UINT32 Ww, UINT32 Wh) {
    UINT64 Bytes = (UINT64)Ww * (UINT64)Wh * sizeof(UINT32);

    return (UINT32)((Bytes + PAGE_SIZE - 1) / PAGE_SIZE);
}

static int EnsureWindowBackupBuf(int Idx) {
    UINT32 Pages;

    if (Idx < 0 || Idx >= MAX_WINS || !gWins[Idx].Active) {
        return 0;
    }
    if (gWins[Idx].Width == 0 || gWins[Idx].Height == 0) {
        return 0;
    }
    Pages = BackupPageCount(gWins[Idx].Width, gWins[Idx].Height);
    if (Pages == 0) {
        return 0;
    }
    if (gWinBackup[Idx] != 0 && gWinBackupPages[Idx] == Pages) {
        return 1;
    }
    if (gWinBackup[Idx] != 0) {
        PhysicalMemoryFreePages(gWinBackup[Idx], gWinBackupPages[Idx]);
        gWinBackup[Idx] = 0;
        gWinBackupPages[Idx] = 0;
    }
    /* 换新页后内容未定义，必须清 Valid，禁止未填充就 WriteRect */
    gWinBackupValid[Idx] = 0;
    gWinBackup[Idx] = (UINT32 *)PhysicalMemoryAllocatePages(Pages);
    if (gWinBackup[Idx] == 0) {
        return 0;
    }
    gWinBackupPages[Idx] = Pages;
    return 1;
}

static void PreallocWindowBackups(void) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (gWins[i].Active) {
            EnsureWindowBackupBuf(i);
        }
    }
}

static void CaptureDragRestoreData(int DragIdx);

static void BackupWindowAt(int Idx) {
    const GUI_WINDOW *Win = &gWins[Idx];
    UINT32 Rw;
    UINT32 Rh;

    gWinBackupValid[Idx] = 0;
    if (Idx < 0 || Idx >= MAX_WINS || !Win->Active) {
        return;
    }
    if (Win->Width == 0 || Win->Height == 0) {
        return;
    }
    Rw = Win->Width;
    Rh = Win->Height;
    if (Win->X + Rw > gScreenW) {
        Rw = gScreenW - Win->X;
    }
    if (Win->Y + Rh > gScreenH) {
        Rh = gScreenH - Win->Y;
    }
    if (Rw == 0 || Rh == 0) {
        return;
    }
    if (!EnsureWindowBackupBuf(Idx)) {
        return;
    }
    HalVideoReadRect(Win->X, Win->Y, Rw, Rh, gWinBackup[Idx]);
    gWinBackupW[Idx] = Rw;
    gWinBackupH[Idx] = Rh;
    gWinBackupValid[Idx] = 1;
}

/* 与 DrawWindowAt 布局一致，用于合成被拖窗下方的干净像素 */
static UINT32 AnalyticWindowPixel(int Idx, UINT32 Px, UINT32 Py) {
    const GUI_WINDOW *W = &gWins[Idx];
    UINT32 Lx;
    UINT32 Ly;

    if (!W->Active) {
        return COLOR_DARK_GRAY;
    }
    if (Px < W->X || Py < W->Y || Px >= W->X + W->Width || Py >= W->Y + W->Height) {
        return COLOR_DARK_GRAY;
    }
    Lx = Px - W->X;
    Ly = Py - W->Y;
    if (Ly < TITLE_HEIGHT) {
        return TitleBarColor(Idx);
    }
    if (Ly == W->Height - 1 || Lx == 0 || Lx == W->Width - 1) {
        return COLOR_WHITE;
    }
    if (Ly >= TITLE_HEIGHT + 2 && Ly < W->Height - 2 &&
        Lx >= 2 && Lx < W->Width - 2) {
        return W->Background;
    }
    return COLOR_WHITE;
}

static UINT32 TopmostBelowDragPixel(UINT32 Px, UINT32 Py, int DragIdx) {
    int i;

    for (i = DragIdx - 1; i >= 0; i--) {
        if (!gWins[i].Active) {
            continue;
        }
        if (Px >= gWins[i].X && Py >= gWins[i].Y &&
            Px < gWins[i].X + gWins[i].Width &&
            Py < gWins[i].Y + gWins[i].Height) {
            return AnalyticWindowPixel(i, Px, Py);
        }
    }
    return COLOR_DARK_GRAY;
}

static int EnsureDragDirtyBuf(UINT32 Ww, UINT32 Hh) {
    UINT64 Need;
    UINT32 Pages;

    if (Ww == 0 || Hh == 0) {
        return 0;
    }
    Need = (UINT64)Ww * (UINT64)Hh;
    if (Need > 0xFFFFFFFFu / sizeof(UINT32)) {
        return 0;
    }
    if (gDragDirty != 0 && gDragDirtyCap >= (UINT32)Need) {
        return 1;
    }
    Pages = BackupPageCount(Ww, Hh);
    if (Pages == 0) {
        return 0;
    }
    if (gDragDirty != 0) {
        PhysicalMemoryFreePages(gDragDirty, gDragDirtyPages);
        gDragDirty = 0;
        gDragDirtyPages = 0;
        gDragDirtyCap = 0;
    }
    gDragDirty = (UINT32 *)PhysicalMemoryAllocatePages(Pages);
    if (gDragDirty == 0) {
        return 0;
    }
    gDragDirtyPages = Pages;
    gDragDirtyCap = (UINT32)(((UINT64)Pages * PAGE_SIZE) / sizeof(UINT32));
    return 1;
}

static int EnsureUnderDragBuf(UINT32 Ww, UINT32 Wh) {
    UINT32 Pages = BackupPageCount(Ww, Wh);

    if (Pages == 0) {
        return 0;
    }
    if (gUnderDrag != 0 && gUnderDragPages == Pages) {
        return 1;
    }
    if (gUnderDrag != 0) {
        PhysicalMemoryFreePages(gUnderDrag, gUnderDragPages);
        gUnderDrag = 0;
        gUnderDragPages = 0;
    }
    gUnderDragValid = 0;
    gUnderDrag = (UINT32 *)PhysicalMemoryAllocatePages(Pages);
    if (gUnderDrag == 0) {
        return 0;
    }
    gUnderDragPages = Pages;
    return 1;
}

static void CaptureDragRestoreData(int DragIdx) {
    UINT32 Pages;
    UINT32 Ly;
    UINT32 Lx;

    gScreenSnapValid = 0;
    gUnderDragValid = 0;
    if (DragIdx < 0 || DragIdx >= MAX_WINS || !gWins[DragIdx].Active) {
        return;
    }
    if (gScreenW == 0 || gScreenH == 0) {
        return;
    }

    gDragStartX = gWins[DragIdx].X;
    gDragStartY = gWins[DragIdx].Y;
    gDragStartW = gWins[DragIdx].Width;
    gDragStartH = gWins[DragIdx].Height;
    if (gDragStartW == 0 || gDragStartH == 0) {
        return;
    }

    Pages = BackupPageCount(gScreenW, gScreenH);
    if (gScreenSnap != 0 && gScreenSnapPages != Pages) {
        PhysicalMemoryFreePages(gScreenSnap, gScreenSnapPages);
        gScreenSnap = 0;
        gScreenSnapPages = 0;
    }
    if (gScreenSnap == 0) {
        gScreenSnap = (UINT32 *)PhysicalMemoryAllocatePages(Pages);
        if (gScreenSnap == 0) {
            return;
        }
        gScreenSnapPages = Pages;
    }
    HalVideoReadRect(0, 0, gScreenW, gScreenH, gScreenSnap);
    gScreenSnapValid = 1;
    /* 预分配脏区缓冲，拖动中避免边合成边申请 */
    EnsureDragDirtyBuf(gScreenW, gScreenH);

    if (!EnsureUnderDragBuf(gDragStartW, gDragStartH)) {
        return;
    }
    for (Ly = 0; Ly < gDragStartH; Ly++) {
        for (Lx = 0; Lx < gDragStartW; Lx++) {
            UINT32 Px = gDragStartX + Lx;
            UINT32 Py = gDragStartY + Ly;

            gUnderDrag[Ly * gDragStartW + Lx] = TopmostBelowDragPixel(Px, Py, DragIdx);
        }
    }
    gUnderDragValid = 1;
}

static void BeginDragBackups(int DragIdx) {
    int i;
    int Overlap = AnyWindowsOverlap();

    if (!Overlap) {
        ResetDragState();
    }
    if (DragIdx < 0 || DragIdx >= MAX_WINS || !gWins[DragIdx].Active) {
        return;
    }
    if (!EnsureWindowBackupBuf(DragIdx)) {
        DebugWrite("gui: drag backup alloc failed\n");
        return;
    }
    for (i = 0; i < MAX_WINS; i++) {
        if (!gWins[i].Active) {
            continue;
        }
        EnsureWindowBackupBuf(i);
        if (Overlap && i != DragIdx && WindowOccludedByOther(i) &&
            gWinBackupValid[i]) {
            continue;
        }
        BackupWindowAt(i);
    }
    if (!gWinBackupValid[DragIdx]) {
        return;
    }
    gDragHasBackup = 1;
    CaptureDragRestoreData(DragIdx);
}

static void StartDragBackups(int DragIdx) {
    HalIrqDisable();
    CursorRestore();
    GuiFocusSave();
    if (gDragHasBackup && AllActiveWindowsHaveValidBackup()) {
        int i;

        /* 屏上内容已正确：只刷新顶层备份并抓快照，避免整屏重贴闪一下 */
        HalVideoClearClip();
        for (i = 0; i < MAX_WINS; i++) {
            if (!gWins[i].Active) {
                continue;
            }
            if (i == DragIdx || !WindowOccludedByOther(i)) {
                BackupWindowAt(i);
            }
        }
        CaptureDragRestoreData(DragIdx);
    } else {
        BeginDragBackups(DragIdx);
    }
    HalIrqEnable();
}

static void PaintWindowFromBackup(int Idx) {
    const GUI_WINDOW *Win = &gWins[Idx];

    if (!Win->Active) {
        return;
    }
    if (gWinBackupValid[Idx] && gWinBackup[Idx] != 0) {
        HalVideoWriteRect(Win->X, Win->Y, gWinBackupW[Idx], gWinBackupH[Idx],
                       gWinBackup[Idx]);
        /* 备份里是拖动前的标题栏色，按当前焦点重画 chrome */
        DrawWindowChromeAt(Idx);
        return;
    }
    DrawWindowAt(Idx);
}

static void ShiftWinBackupsUp(int From, int To) {
    UINT32 *Buf = gWinBackup[From];
    UINT32 Bw = gWinBackupW[From];
    UINT32 Bh = gWinBackupH[From];
    UINT32 Bp = gWinBackupPages[From];
    int Bv = gWinBackupValid[From];
    int J;

    for (J = From; J < To; J++) {
        gWinBackup[J] = gWinBackup[J + 1];
        gWinBackupW[J] = gWinBackupW[J + 1];
        gWinBackupH[J] = gWinBackupH[J + 1];
        gWinBackupPages[J] = gWinBackupPages[J + 1];
        gWinBackupValid[J] = gWinBackupValid[J + 1];
    }
    gWinBackup[To] = Buf;
    gWinBackupW[To] = Bw;
    gWinBackupH[To] = Bh;
    gWinBackupPages[To] = Bp;
    gWinBackupValid[To] = Bv;
}

static int RectIntersects(UINT32 Ax, UINT32 Ay, UINT32 Aw, UINT32 Ah,
                          UINT32 Bx, UINT32 By, UINT32 Bw, UINT32 Bh) {
    if (Aw == 0 || Ah == 0 || Bw == 0 || Bh == 0) {
        return 0;
    }
    return Ax < Bx + Bw && Bx < Ax + Aw && Ay < By + Bh && By < Ay + Ah;
}

static void ClipRectToScreen(UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H) {
    if (*W == 0 || *H == 0 || gScreenW == 0 || gScreenH == 0) {
        *W = 0;
        return;
    }
    if (*X >= gScreenW || *Y >= gScreenH) {
        *W = 0;
        *H = 0;
        return;
    }
    if (*X + *W > gScreenW) {
        *W = gScreenW - *X;
    }
    if (*Y + *H > gScreenH) {
        *H = gScreenH - *Y;
    }
}

static UINT32 SampleWindowBackupPixel(int Idx, UINT32 Px, UINT32 Py) {
    const GUI_WINDOW *W = &gWins[Idx];
    UINT32 Bw;
    UINT32 Bh;
    UINT32 Lx;
    UINT32 Ly;

    if (!gWinBackupValid[Idx] || gWinBackup[Idx] == 0 || !W->Active) {
        return COLOR_DARK_GRAY;
    }
    Bw = gWinBackupW[Idx];
    Bh = gWinBackupH[Idx];
    if (Px < W->X || Py < W->Y) {
        return COLOR_DARK_GRAY;
    }
    Lx = Px - W->X;
    Ly = Py - W->Y;
    if (Lx >= Bw || Ly >= Bh) {
        return COLOR_DARK_GRAY;
    }
    return gWinBackup[Idx][Ly * Bw + Lx];
}

static int WindowBackupCoversPixel(int Idx, UINT32 Px, UINT32 Py) {
    const GUI_WINDOW *W = &gWins[Idx];

    if (!gWinBackupValid[Idx] || !W->Active) {
        return 0;
    }
    return Px >= W->X && Py >= W->Y &&
           Px < W->X + gWinBackupW[Idx] && Py < W->Y + gWinBackupH[Idx];
}

/* 拖动脏区单像素合成：新位置用被拖窗备份；露出区域用拖动前快照/下方干净层 */
static UINT32 CompositeDragPixel(UINT32 Px, UINT32 Py, int DragIdx,
                                 UINT32 Nx, UINT32 Ny, UINT32 Nw, UINT32 Nh) {
    int i;

    if (Px >= Nx && Px < Nx + Nw && Py >= Ny && Py < Ny + Nh &&
        gWinBackupValid[DragIdx] && gWinBackup[DragIdx] != 0) {
        UINT32 Bw = gWinBackupW[DragIdx];
        UINT32 Bh = gWinBackupH[DragIdx];
        UINT32 Lx = Px - Nx;
        UINT32 Ly = Py - Ny;

        if (Lx < Bw && Ly < Bh) {
            return gWinBackup[DragIdx][Ly * Bw + Lx];
        }
    }

    /* 起始 footprint：用 under-drag，勿用含拖动窗本体的 screen snap */
    if (gUnderDragValid &&
        Px >= gDragStartX && Px < gDragStartX + gDragStartW &&
        Py >= gDragStartY && Py < gDragStartY + gDragStartH) {
        UINT32 Ux = Px - gDragStartX;
        UINT32 Uy = Py - gDragStartY;

        return gUnderDrag[Uy * gDragStartW + Ux];
    }

    /* 拖动前全屏快照是露出区域的权威来源（被挡窗备份在重叠区是脏的） */
    if (gScreenSnapValid && Px < gScreenW && Py < gScreenH) {
        return gScreenSnap[Py * gScreenW + Px];
    }

    for (i = MAX_WINS - 1; i >= 0; i--) {
        if (i == DragIdx || !gWins[i].Active) {
            continue;
        }
        if (WindowBackupCoversPixel(i, Px, Py)) {
            return SampleWindowBackupPixel(i, Px, Py);
        }
    }
    return COLOR_DARK_GRAY;
}

/* 旧/新 footprint 并集一次扫描线写出，避免先清灰再全窗重贴的两步闪屏 */
static void CompositeDragDirtyRegion(int DragIdx, UINT32 OldX, UINT32 OldY,
                                     UINT32 Ww, UINT32 Wh) {
    const GUI_WINDOW *Drag = &gWins[DragIdx];
    UINT32 Nx = Drag->X;
    UINT32 Ny = Drag->Y;
    UINT32 DuX;
    UINT32 DuY;
    UINT32 DuW;
    UINT32 DuH;
    UINT32 Right;
    UINT32 Bottom;
    UINT32 Row;

    if (Ww == 0 || Wh == 0) {
        return;
    }
    DuX = OldX < Nx ? OldX : Nx;
    DuY = OldY < Ny ? OldY : Ny;
    Right = OldX + Ww;
    if (Nx + Ww > Right) {
        Right = Nx + Ww;
    }
    Bottom = OldY + Wh;
    if (Ny + Wh > Bottom) {
        Bottom = Ny + Wh;
    }
    DuW = Right - DuX;
    DuH = Bottom - DuY;
    if (DuX > DRAG_BORDER_PAD) {
        DuX -= DRAG_BORDER_PAD;
        DuW += DRAG_BORDER_PAD;
    }
    if (DuY > DRAG_BORDER_PAD) {
        DuY -= DRAG_BORDER_PAD;
        DuH += DRAG_BORDER_PAD;
    }
    DuW += DRAG_BORDER_PAD;
    DuH += DRAG_BORDER_PAD;
    ClipRectToScreen(&DuX, &DuY, &DuW, &DuH);
    if (DuW == 0 || DuH == 0) {
        return;
    }
    if (EnsureDragDirtyBuf(DuW, DuH)) {
        for (Row = 0; Row < DuH; Row++) {
            UINT32 Py = DuY + Row;
            UINT32 Col;

            for (Col = 0; Col < DuW; Col++) {
                UINT32 Px = DuX + Col;

                gDragDirty[Row * DuW + Col] =
                    CompositeDragPixel(Px, Py, DragIdx, Nx, Ny, Ww, Wh);
            }
        }
        HalVideoWriteRect(DuX, DuY, DuW, DuH, gDragDirty);
        return;
    }
    if (DuW > DRAG_ROW_MAX) {
        DuW = DRAG_ROW_MAX;
    }
    for (Row = 0; Row < DuH; Row++) {
        UINT32 Py = DuY + Row;
        UINT32 Col;

        for (Col = 0; Col < DuW; Col++) {
            UINT32 Px = DuX + Col;

            gDragRowBuf[Col] = CompositeDragPixel(Px, Py, DragIdx, Nx, Ny, Ww, Wh);
        }
        HalVideoWriteRect(DuX, Py, DuW, 1, gDragRowBuf);
    }
}

static void RestoreWindowsInFootprint(UINT32 Fx, UINT32 Fy, UINT32 Fw, UINT32 Fh,
                                      int SkipIdx) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (!gWins[i].Active || i == SkipIdx) {
            continue;
        }
        if (RectIntersects(gWins[i].X, gWins[i].Y, gWins[i].Width, gWins[i].Height,
                           Fx, Fy, Fw, Fh)) {
            PaintWindowFromBackup(i);
        }
    }
}

/* 清除整片旧 footprint：先恢复被盖住的其它窗，再填桌面色 */
static void ClearOldDragFootprint(UINT32 Ox, UINT32 Oy, UINT32 Ww, UINT32 Wh,
                                  int DragIdx) {
    RestoreWindowsInFootprint(Ox, Oy, Ww, Wh, DragIdx);
    FillDesktopRectClipped(Ox, Oy, Ww, Wh);
}

/* 按 z 序从备份重贴全部窗口（DragIdx<0 时按数组序；否则被拖窗最后画） */
static void PaintAllWindowsFromBackup(int DragIdx) {
    int i;

    if (DragIdx < 0) {
        for (i = 0; i < MAX_WINS; i++) {
            if (gWins[i].Active) {
                PaintWindowFromBackup(i);
            }
        }
        return;
    }
    for (i = 0; i < MAX_WINS; i++) {
        if (!gWins[i].Active || i == DragIdx) {
            continue;
        }
        PaintWindowFromBackup(i);
    }
    if (DragIdx >= 0 && DragIdx < MAX_WINS && gWins[DragIdx].Active) {
        PaintWindowFromBackup(DragIdx);
    }
}

/* 按 z 序重画全部窗口（被拖窗最后画；备份不可用时回退） */
static void PaintAllWindowsDraw(int DragIdx) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (!gWins[i].Active || i == DragIdx) {
            continue;
        }
        DrawWindowAt(i);
    }
    if (DragIdx >= 0 && DragIdx < MAX_WINS && gWins[DragIdx].Active) {
        DrawWindowAt(DragIdx);
    }
}

/* 拖动一帧：脏区并集内单次合成（无中间灰底） */
static void RedrawDragFrame(int DragIdx, UINT32 OldX, UINT32 OldY) {
    const GUI_WINDOW *Drag = &gWins[DragIdx];

    HalVideoClearClip();
    CompositeDragDirtyRegion(DragIdx, OldX, OldY, Drag->Width, Drag->Height);
}

/* 窗口必须完整留在屏内 */
static void ClampWindowPos(const GUI_WINDOW *W, INT32 *X, INT32 *Y) {
    INT32 MaxX = (INT32)gScreenW - (INT32)W->Width;
    INT32 MaxY = (INT32)gScreenH - (INT32)W->Height;

    if (MaxX < 0) {
        MaxX = 0;
    }
    if (MaxY < 0) {
        MaxY = 0;
    }
    if (*X < 0) {
        *X = 0;
    }
    if (*Y < 0) {
        *Y = 0;
    }
    if (*X > MaxX) {
        *X = MaxX;
    }
    if (*Y > MaxY) {
        *Y = MaxY;
    }
}

static int PointOnAnyClose(UINT32 X, UINT32 Y) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (gWins[i].Active && PointInClose(&gWins[i], X, Y)) {
            return 1;
        }
    }
    return 0;
}

static void MoveWindowTo(int Idx, UINT32 NewX, UINT32 NewY) {
    GUI_WINDOW *W;
    UINT32 Ox;
    UINT32 Oy;
    UINT32 Ww;
    UINT32 Wh;
    INT32 Dx;
    INT32 Dy;

    if (Idx < 0 || Idx >= MAX_WINS) {
        return;
    }
    W = &gWins[Idx];
    if (!W->Active) {
        return;
    }
    Ox = W->X;
    Oy = W->Y;
    Ww = W->Width;
    Wh = W->Height;
    if (NewX == Ox && NewY == Oy) {
        return;
    }
    if (NewX + Ww > gScreenW || NewY + Wh > gScreenH) {
        return;
    }

    Dx = (INT32)NewX - (INT32)Ox;
    Dy = (INT32)NewY - (INT32)Oy;

    HalIrqDisable();
    if (gCursorVisible) {
        CursorRestore();
    }
    if (gDragHasBackup) {
        W->X = NewX;
        W->Y = NewY;
        if (W->TermSet) {
            W->TermX = (UINT32)((INT32)W->TermX + Dx);
            W->TermY = (UINT32)((INT32)W->TermY + Dy);
        }
        RedrawDragFrame(Idx, Ox, Oy);
    } else if (gDragWin >= 0) {
        W->X = NewX;
        W->Y = NewY;
        if (W->TermSet) {
            W->TermX = (UINT32)((INT32)W->TermX + Dx);
            W->TermY = (UINT32)((INT32)W->TermY + Dy);
        }
        HalVideoClearClip();
        if (gWinBackupValid[Idx]) {
            CompositeDragDirtyRegion(Idx, Ox, Oy, Ww, Wh);
        } else {
            ClearOldDragFootprint(Ox, Oy, Ww, Wh, Idx);
            PaintAllWindowsDraw(Idx);
        }
    } else {
        HalVideoCopyRect(Ox, Oy, NewX, NewY, Ww, Wh);
        W->X = NewX;
        W->Y = NewY;
        if (W->TermSet) {
            W->TermX = (UINT32)((INT32)W->TermX + Dx);
            W->TermY = (UINT32)((INT32)W->TermY + Dy);
        }
        ClearOldDragFootprint(Ox, Oy, Ww, Wh, Idx);
        RefreshOtherChrome(Idx);
        DrawWindowChromeAt(Idx);
    }
    if (gDragWin < 0) {
        CursorPaint();
    }
    HalIrqEnable();
}

/* 将窗口移到最前（数组后部 = 绘制在上层） */
static void RaiseWindow(int Idx) {
    int Top = Idx;
    int J;
    int HasBackup = 0;

    for (J = Idx + 1; J < MAX_WINS; J++) {
        if (gWins[J].Active) {
            Top = J;
        }
    }
    if (Top == Idx) {
        gFocusWin = Idx;
        return;
    }
    for (J = 0; J < MAX_WINS; J++) {
        if (gWinBackupValid[J] || gWinBackup[J] != 0) {
            HasBackup = 1;
            break;
        }
    }
    {
        WinCopy(&gWinSwap, &gWins[Idx]);
        for (J = Idx; J < Top; J++) {
            WinCopy(&gWins[J], &gWins[J + 1]);
        }
        WinCopy(&gWins[Top], &gWinSwap);
        /* 窗口与备份必须一起挪，否则会把别的窗备份贴到错误位置（花屏） */
        if (HasBackup || gDragHasBackup) {
            ShiftWinBackupsUp(Idx, Top);
        }
        gFocusWin = Top;
    }
}

int GuiFocusClient(UINT32 *X, UINT32 *Y, UINT32 *Width, UINT32 *Height, UINT32 *Background) {
    const GUI_WINDOW *Win;
    UINT32 Pad = GUI_CLIENT_PAD;

    if (gFocusWin < 0 || gFocusWin >= MAX_WINS || !gWins[gFocusWin].Active) {
        return 0;
    }
    Win = &gWins[gFocusWin];
    if (X) {
        *X = Win->X + 2 + Pad;
    }
    if (Y) {
        *Y = Win->Y + TITLE_HEIGHT + Pad;
    }
    if (Width) {
        *Width = (Win->Width > 4 + Pad * 2) ? Win->Width - 4 - Pad * 2 : 0;
    }
    if (Height) {
        *Height = (Win->Height > TITLE_HEIGHT + 2 + Pad * 2) ?
             Win->Height - TITLE_HEIGHT - 2 - Pad * 2 : 0;
    }
    if (Background) {
        *Background = Win->Background;
    }
    if (Width && Height) {
        return *Width > 0 && *Height > 0;
    }
    return 1;
}

void GuiFocusSave(void) {
    if (gFocusWin < 0 || gFocusWin >= MAX_WINS || !gWins[gFocusWin].Active) {
        return;
    }
    ConsoleFocusSave();
    HalVideoGetTextCursor(&gWins[gFocusWin].TermX, &gWins[gFocusWin].TermY);
    gWins[gFocusWin].TermSet = 1;
}

void GuiConsolePull(char *Line, int *Len, int *WaitPrompt) {
    GUI_WINDOW *Win;

    if (gFocusWin < 0 || gFocusWin >= MAX_WINS || !gWins[gFocusWin].Active) {
        if (Len) {
            *Len = 0;
        }
        if (WaitPrompt) {
            *WaitPrompt = 0;
        }
        if (Line) {
            Line[0] = 0;
        }
        return;
    }
    Win = &gWins[gFocusWin];
    if (Line) {
        int i;
        for (i = 0; i < Win->InputLen && i < GUI_INPUT_LINE_MAX - 1; i++) {
            Line[i] = Win->InputLine[i];
        }
        Line[i] = 0;
    }
    if (Len) {
        *Len = Win->InputLen;
    }
    if (WaitPrompt) {
        *WaitPrompt = Win->WaitPrompt;
    }
}

void GuiConsolePush(const char *Line, int Len, int WaitPrompt) {
    GUI_WINDOW *Win;
    int i;

    if (gFocusWin < 0 || gFocusWin >= MAX_WINS || !gWins[gFocusWin].Active) {
        return;
    }
    Win = &gWins[gFocusWin];
    if (Len >= GUI_INPUT_LINE_MAX) {
        Len = GUI_INPUT_LINE_MAX - 1;
    }
    Win->InputLen = Len;
    Win->WaitPrompt = WaitPrompt;
    for (i = 0; i < Len; i++) {
        Win->InputLine[i] = Line[i];
    }
    Win->InputLine[Len] = 0;
}

int GuiConsoleNeedsPrompt(void) {
    if (gFocusWin < 0 || gFocusWin >= MAX_WINS || !gWins[gFocusWin].Active) {
        return 0;
    }
    return !gWins[gFocusWin].PromptShown;
}

void GuiConsoleMarkPrompt(void) {
    if (gFocusWin < 0 || gFocusWin >= MAX_WINS || !gWins[gFocusWin].Active) {
        return;
    }
    gWins[gFocusWin].PromptShown = 1;
}

int GuiConsoleHasDisplay(void) {
    if (gFocusWin < 0 || gFocusWin >= MAX_WINS || !gWins[gFocusWin].Active) {
        return 0;
    }
    return gWins[gFocusWin].TermSet;
}

void GuiFocusApply(void) {
    GuiFocusApplyClip();
    ConsoleFocusLoad();
}

void GuiFocusApplyClip(void) {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Bg;
    GUI_WINDOW *Win;
    UINT32 Tx;
    UINT32 Ty;

    if (!GuiFocusClient(&X, &Y, &W, &H, &Bg)) {
        HalVideoClearClip();
        return;
    }
    HalVideoSetClipRegion(X, Y, W, H, Bg);
    Win = &gWins[gFocusWin];
    if (!Win->TermSet) {
        HalVideoSetTextCursor(X, Y);
        return;
    }
    Tx = Win->TermX;
    Ty = Win->TermY;
    if (Tx < X) {
        Tx = X;
    }
    if (Ty < Y) {
        Ty = Y;
    }
    if (W > 0 && Tx >= X + W) {
        Tx = X + W - 1;
    }
    if (H > 0 && Ty >= Y + H) {
        Ty = Y + H - 1;
    }
    HalVideoSetTextCursor(Tx, Ty);
    Win->TermX = Tx;
    Win->TermY = Ty;
}

static int PixelCoveredByHigherWindow(int Idx, UINT32 Px, UINT32 Py) {
    int j;

    for (j = Idx + 1; j < MAX_WINS; j++) {
        if (gWins[j].Active && PointInWindow(&gWins[j], Px, Py)) {
            return 1;
        }
    }
    return 0;
}

void GuiBackupSyncRect(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    const GUI_WINDOW *Win;
    UINT32 Row;
    UINT32 Col;
    UINT32 Bw;
    UINT32 Bh;
    UINT32 Px;
    UINT32 Py;
    UINT32 Bx;
    UINT32 By;
    UINT32 Dst;
    int Idx;

    if (gFocusWin < 0 || gFocusWin >= MAX_WINS || !gWins[gFocusWin].Active) {
        return;
    }
    Idx = gFocusWin;
    if (!gWinBackupValid[Idx] || gWinBackup[Idx] == 0) {
        return;
    }
    Win = &gWins[Idx];
    Bw = gWinBackupW[Idx];
    Bh = gWinBackupH[Idx];
    if (Bw == 0 || Bh == 0) {
        return;
    }
    for (Row = 0; Row < H; Row++) {
        Py = Y + Row;
        if (Py < Win->Y || Py >= Win->Y + Bh) {
            continue;
        }
        for (Col = 0; Col < W; Col++) {
            Px = X + Col;
            if (Px < Win->X || Px >= Win->X + Bw) {
                continue;
            }
            if (PixelCoveredByHigherWindow(Idx, Px, Py)) {
                continue;
            }
            Bx = Px - Win->X;
            By = Py - Win->Y;
            Dst = By * Bw + Bx;
            gWinBackup[Idx][Dst] = HalVideoReadPixel(Px, Py);
        }
    }
}

void GuiFocusSyncCursor(void) {
    if (gFocusWin < 0 || gFocusWin >= MAX_WINS || !gWins[gFocusWin].Active) {
        return;
    }
    HalVideoGetTextCursor(&gWins[gFocusWin].TermX, &gWins[gFocusWin].TermY);
    gWins[gFocusWin].TermSet = 1;
}

void GuiFocusClearClient(void) {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Bg;
    GUI_WINDOW *Win;

    if (!GuiFocusClient(&X, &Y, &W, &H, &Bg)) {
        return;
    }
    GfxIrqEnter();
    CursorRestore();
    HalVideoFillRect(X, Y, W, H, Bg);
    HalVideoSetClipOrigin(X, Y, W, H, Bg);
    Win = &gWins[gFocusWin];
    Win->TermX = X;
    Win->TermY = Y;
    Win->TermSet = 1;
    GuiBackupSyncRect(X, Y, W, H);
    CursorPaint();
    GfxIrqLeave();
}

int GuiShellAcceptsInput(void) {
    return gFocusWin >= 0 && gFocusWin < MAX_WINS && gWins[gFocusWin].Active;
}

int GuiShellWindowActive(int Idx) {
    return Idx >= 0 && Idx < MAX_WINS && gWins[Idx].Active;
}

void GuiSetFocusWin(int Idx) {
    if (Idx >= 0 && Idx < MAX_WINS && gWins[Idx].Active) {
        gFocusWin = Idx;
    }
}

void GuiFocusHome(void) {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Bg;
    GUI_WINDOW *Win;

    if (!GuiFocusClient(&X, &Y, &W, &H, &Bg)) {
        HalVideoClearClip();
        return;
    }
    HalVideoSetClipOrigin(X, Y, W, H, Bg);
    if (gFocusWin >= 0 && gFocusWin < MAX_WINS) {
        Win = &gWins[gFocusWin];
        Win->TermX = X;
        Win->TermY = Y;
        Win->TermSet = 1;
    }
}

void GuiRedraw(void) {
    int i;

    GfxIrqEnter();
    CursorRestore();
    UiFillRectangle(0, 0, gScreenW, gScreenH, COLOR_DARK_GRAY);
    for (i = 0; i < MAX_WINS; i++) {
        DrawWindowAt(i);
    }
    CursorPaint();
    GfxIrqLeave();
}

void GuiInit(void) {
    HalVideoGetSize(&gScreenW, &gScreenH);
    if (gScreenW == 0) {
        gScreenW = 1024;
        gScreenH = 768;
    }
    gCursorX = gScreenW / 2;
    gCursorY = gScreenH / 2;
    gFocusWin = 0;
    gCursorVisible = 0;
    gDragWin = -1;

    {
        /* 略小于全屏，露出桌面，方便演示拖动 */
        UINT32 Margin = 48;
        UINT32 ShellW = gScreenW > Margin * 2 + 200 ?
                        gScreenW - Margin * 2 : gScreenW - 32;
        UINT32 ShellH = gScreenH > Margin * 2 + 120 ?
                        gScreenH - Margin * 2 : gScreenH - 32;
        int i;

        gWins[0].Active = 1;
        gWins[0].X = Margin;
        gWins[0].Y = Margin;
        gWins[0].Width = ShellW;
        gWins[0].Height = ShellH;
        gWins[0].Background = COLOR_LIGHT_GRAY;
        gWins[0].Title = "ToyOS Shell";
        gWins[0].TermSet = 0;
        gWins[0].InputLen = 0;
        gWins[0].WaitPrompt = 0;
        gWins[0].PromptShown = 0;

        /* 其余槽位空闲；需要第二窗时可再开（当前无运行时 new 命令） */
        for (i = 1; i < MAX_WINS; i++) {
            gWins[i].Active = 0;
            gWins[i].TermSet = 0;
            gWins[i].InputLen = 0;
            gWins[i].WaitPrompt = 0;
            gWins[i].PromptShown = 0;
            gWins[i].InputLine[0] = 0;
            gWins[i].Title = "Shell";
        }
    }

    gFocusWin = 0;
    PreallocWindowBackups();
    GuiRedraw();
    DebugWrite("gui: desktop ready (1 shell window)\n");
}

void GuiOnArrowKey(UINT8 Key) {
    UINT32 X = gCursorX;
    UINT32 Y = gCursorY;
    UINT32 Step = 8;

    if (Key == 0x50 && X >= Step) {
        X -= Step;
    } else if (Key == 0x4F && X + Step < gScreenW) {
        X += Step;
    } else if (Key == 0x52 && Y >= Step) {
        Y -= Step;
    } else if (Key == 0x51 && Y + Step < gScreenH) {
        Y += Step;
    } else if (Key == 0x28) {
        GUI_MOUSE_STATE M;
        M.X = gCursorX;
        M.Y = gCursorY;
        M.Buttons = 1;
        GuiOnMouse(&M);
        M.Buttons = 0;
        GuiOnMouse(&M);
        return;
    } else {
        return;
    }
    CursorMove(X, Y);
}

int GuiHandleClick(UINT32 X, UINT32 Y) {
    int i;

    /* 关闭钮可能被其它窗口挡住；先扫一遍所有窗口的 × 区域 */
    for (i = MAX_WINS - 1; i >= 0; i--) {
        if (gWins[i].Active && PointInClose(&gWins[i], X, Y)) {
            CloseWindow(i);
            return 1;
        }
    }

    for (i = MAX_WINS - 1; i >= 0; i--) {
        if (!PointInWindow(&gWins[i], X, Y)) {
            continue;
        }
        GuiFocusSave();
        RaiseWindow(i);
        SyncWindowVisuals();
        GuiFocusApply();
        DebugWrite("gui: focus ");
        DebugWrite(gWins[gFocusWin].Title);
        DebugWrite("\n");

        if (PointInTitle(&gWins[gFocusWin], X, Y) &&
            !PointOnAnyClose(X, Y)) {
            HalIrqDisable();
            CursorRestore();
            HalIrqEnable();
            RaiseWindow(gFocusWin);
            gDragWin = gFocusWin;
            gDragOffX = (INT32)X - (INT32)gWins[gFocusWin].X;
            gDragOffY = (INT32)Y - (INT32)gWins[gFocusWin].Y;
            StartDragBackups(gDragWin);
        }
        return 1;
    }
    return 0;
}

static void GuiDragUpdate(UINT32 X, UINT32 Y) {
    INT32 Nx;
    INT32 Ny;
    INT32 Dx;
    INT32 Dy;
    GUI_WINDOW *W;

    if (gDragWin < 0 || gDragWin >= MAX_WINS || !gWins[gDragWin].Active) {
        return;
    }
    W = &gWins[gDragWin];
    Nx = (INT32)X - gDragOffX;
    Ny = (INT32)Y - gDragOffY;
    ClampWindowPos(W, &Nx, &Ny);

    Dx = Nx - (INT32)W->X;
    Dy = Ny - (INT32)W->Y;
    if (Dx < 0) {
        Dx = -Dx;
    }
    if (Dy < 0) {
        Dy = -Dy;
    }
    if ((UINT32)Dx < DRAG_MIN_STEP && (UINT32)Dy < DRAG_MIN_STEP) {
        return;
    }
    MoveWindowTo(gDragWin, (UINT32)Nx, (UINT32)Ny);
}

static void GuiDragEnd(void) {
    int DragIdx = gDragWin;

    gDragWin = -1;
    if (DragIdx >= 0 && gWins[DragIdx].Active) {
        INT32 Nx = (INT32)gCursorX - gDragOffX;
        INT32 Ny = (INT32)gCursorY - gDragOffY;

        ClampWindowPos(&gWins[DragIdx], &Nx, &Ny);
        MoveWindowTo(DragIdx, (UINT32)Nx, (UINT32)Ny);
        RaiseWindow(DragIdx);
        /* RaiseWindow 后焦点在 gFocusWin；下层 chrome 遮挡裁剪，勿整窗贴备份 */
        HalIrqDisable();
        if (gCursorVisible) {
            CursorRestore();
        }
        HalVideoClearClip();
        if (gFocusWin >= 0) {
            RefreshOtherChrome(gFocusWin);
            DrawWindowChromeAt(gFocusWin);
            GuiFocusApplyClip();
        }
        CursorPaint();
        HalIrqEnable();
    }
    if (!AnyWindowsOverlap()) {
        ResetDragState();
    }
}

void GuiPointerMove(UINT32 X, UINT32 Y) {
    CursorMove(X, Y);
    if (gDragWin >= 0 && (gCursorBtn & 1)) {
        GuiDragUpdate(X, Y);
    }
}

/* 帧缓冲绘制前：关中断并擦掉光标（避免 save-under 采到十字像素） */
void GuiFrameBufferBegin(void) {
    GfxIrqEnter();
    CursorRestore();
}

/* 帧缓冲绘制后：重画光标；仅恢复进入 Begin 前已开启的中断 */
void GuiFrameBufferEnd(void) {
    CursorPaint();
    GfxIrqLeave();
}

void GuiCursorPaint(void) {
    GfxIrqEnter();
    CursorRestore();
    CursorPaint();
    GfxIrqLeave();
}

void GuiCursorHide(void) {
    GfxIrqEnter();
    CursorRestore();
    GfxIrqLeave();
}

void GuiCursorShow(void) {
    GfxIrqEnter();
    CursorPaint();
    GfxIrqLeave();
}

void GuiOnMouse(const GUI_MOUSE_STATE *Mouse) {
    static UINT8 PrevBtn;

    gCursorBtn = Mouse->Buttons;
    GuiPointerMove(Mouse->X, Mouse->Y);

    if ((Mouse->Buttons & 1) && !(PrevBtn & 1)) {
        GuiHandleClick(gCursorX, gCursorY);
    }
    if (!(Mouse->Buttons & 1) && (PrevBtn & 1)) {
        GuiDragEnd();
    }
    PrevBtn = Mouse->Buttons;
}

/* 从 XHCI 鼠标队列取报告并更新光标（Shell/Gui 任务均可调用） */
void GuiPollMouse(void) {
    HAL_MOUSE_REPORT Raw;
    UINT32 Sw;
    UINT32 Sh;
    static UINT8 PrevBtn;

    if (!HalMousePresent()) {
        return;
    }

    HalVideoGetSize(&Sw, &Sh);
    if (Sw == 0) {
        Sw = gScreenW ? gScreenW : 1024;
    }
    if (Sh == 0) {
        Sh = gScreenH ? gScreenH : 768;
    }

    HalInputPoll();

    while (HalMouseDequeue(&Raw)) {
        UINT32 X;
        UINT32 Y;

        if (Raw.X > Sw || Raw.Y > Sh) {
            X = Raw.X * Sw / 32767;
            Y = Raw.Y * Sh / 32767;
        } else {
            X = Raw.X;
            Y = Raw.Y;
        }
        if (X >= Sw) {
            X = Sw > 0 ? Sw - 1 : 0;
        }
        if (Y >= Sh) {
            Y = Sh > 0 ? Sh - 1 : 0;
        }

        gCursorBtn = Raw.Buttons;
        CursorMove(X, Y);

        if ((Raw.Buttons & 1) && !(PrevBtn & 1)) {
            GuiHandleClick(X, Y);
        } else if ((Raw.Buttons & 1) && gDragWin >= 0) {
            GuiDragUpdate(X, Y);
        }
        if (!(Raw.Buttons & 1) && (PrevBtn & 1)) {
            GuiDragEnd();
        }
        PrevBtn = Raw.Buttons;
    }
}
