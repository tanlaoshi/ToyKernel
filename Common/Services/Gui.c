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
#include "Theme.h"
#include "Font.h"
#include "Desktop.h"
#include "SettingsUi.h"
#include "FilesUi.h"
#include "Locale.h"

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
    GUI_WIN_KIND Kind; /* PR-D3 */
    UINT32   X;
    UINT32   Y;
    UINT32   Width;
    UINT32   Height;
    UINT32   Background;
    const char *Title;
    UINT32   TermX;   /* 客户区内相对文本光标 X（非屏幕绝对坐标） */
    UINT32   TermY;   /* 客户区内相对文本光标 Y */
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

/* 标题栏拖动：按下时记录窗口下标与光标相对偏移；移动后才抓备份 */
static int    gDragWin = -1;
static INT32  gDragOffX;
static INT32  gDragOffY;
static int    gDragArmed;
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
    *Dst = *Src;
}

/* 保存进入绘制区前的中断状态，避免误恢复导致嵌套中断（PR-A5：HalIrqSave） */
static int    gGfxLockDepth;
static UINT64 gGfxIrqFlags;
/* G7：长合成开中断时禁止嵌套 GuiOnMouse，避免 Capture 半成品进备份 */
static int    gComposeBusy;
/* 主题一次合成：推迟 Present，避免下层 Shell 中途盖住上层 Settings */
static int    gDeferPresent;

static void GfxIrqEnter(void) {
    if (gGfxLockDepth++ == 0) {
        gGfxIrqFlags = HalIrqSave();
    }
}

static void GfxIrqLeave(void) {
    if (gGfxLockDepth > 0 && --gGfxLockDepth == 0) {
        HalIrqRestore(gGfxIrqFlags);
    }
}

static void ComposeBegin(void) {
    gComposeBusy++;
}

static void ComposeEnd(void) {
    if (gComposeBusy > 0) {
        gComposeBusy--;
    }
}

/* 主题合成中推迟 Present；拖动等路径仍立即提交 */
static void GfxPresent(void) {
    if (gDeferPresent) {
        return;
    }
    HalVideoPresent();
}

static void DrawWindowAt(int Idx);
static void DrawWindowAtEx(int Idx, int Occlude);
static void BackupWindowAt(int Idx);
static void BackupWindowAtEx(int Idx, int ForceFull);
static void RaiseWindow(int Idx);
static int RectIntersects(UINT32 Ax, UINT32 Ay, UINT32 Aw, UINT32 Ah,
                          UINT32 Bx, UINT32 By, UINT32 Bw, UINT32 Bh);
static void RefreshOtherChrome(int SkipIdx);
static void CursorRestore(void);
static void CursorPaint(void);
static void SyncWindowVisuals(void);
static UINT32 SampleWindowBackupPixel(int Idx, UINT32 Px, UINT32 Py);
static void DrawWindowChromeAt(int Idx);
static void PaintWindowFromBackup(int Idx);
static int PixelCoveredByHigherWindow(int Idx, UINT32 Px, UINT32 Py);
static int PixelOccludedByAbove(int Idx, UINT32 X, UINT32 Y);
static int WindowOccludedByOther(int Idx);
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
                HalVideoDrawPixelRaw(PxA, PyA, COLOR_WHITE);
            }
            if (!PixelOccludedByAbove(Idx, PxB, PyB)) {
                HalVideoDrawPixelRaw(PxB, PyB, COLOR_WHITE);
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

/*
 * ClearDesktop：先铺桌面再贴窗。拖动结束后必须清底，否则旧 footprint 外的
 * 标题栏/关闭钮残影不会被「只贴窗矩形」的路径擦掉。
 */
static void SyncWindowVisualsEx(int ClearDesktop) {
    int i;

    ComposeBegin();
    GfxIrqEnter();
    CursorRestore();
    GfxIrqLeave();
    HalVideoClearClip();
    if (ClearDesktop) {
        UiFillRectangle(0, 0, gScreenW, gScreenH, ThemeDesktopBg());
        DesktopDraw();
    }
    for (i = 0; i < MAX_WINS; i++) {
        if (!gWins[i].Active) {
            continue;
        }
        if (gWinBackupValid[i] && gWinBackup[i] != 0) {
            PaintWindowFromBackup(i);
        } else if (!ClearDesktop && WindowOccludedByOther(i)) {
            /*
             * 未清桌面时：被挡窗勿 DrawWindowAt（会把露出客户区抹灰）。
             * 已清桌面时：必须满窗覆盖，否则桌面图标会透进客户区（空色块）。
             */
            DrawWindowChromeAt(i);
        } else {
            DrawWindowAt(i);
        }
    }
    GfxIrqEnter();
    CursorPaint();
    HalVideoPresent(); /* PR-G9：合成结束提交脏区 */
    GfxIrqLeave();
    ComposeEnd();
}

static void SyncWindowVisuals(void) {
    SyncWindowVisualsEx(0);
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
    gWins[Idx].Kind = GUI_WIN_NONE;
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
    GfxIrqEnter();
    CursorRestore();
    GfxIrqLeave();
    ComposeBegin();
    HalVideoClearClip();
    UiFillRectangle(X, Y, Ww, Wh, ThemeDesktopBg());
    DesktopDrawRect(X, Y, Ww, Wh);
    for (i = 0; i < MAX_WINS; i++) {
        if (!gWins[i].Active) {
            continue;
        }
        if (RectIntersects(gWins[i].X, gWins[i].Y, gWins[i].Width, gWins[i].Height,
                           X, Y, Ww, Wh)) {
            /* 恢复被关窗盖住的客户区，勿只重画标题栏 */
            PaintWindowFromBackup(i);
        }
    }
    ComposeEnd();
    GfxIrqEnter();
    CursorPaint();
    HalVideoPresent();
    GfxIrqLeave();
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

    /* 必须 Raw：客户区 clip 开启时普通 DrawPixel 会让窗外光标消失 */
    for (i = -CURSOR_HALF; i <= CURSOR_HALF; i++) {
        int Px = (int)X + i;
        int Py = (int)Y + i;
        if (Px >= 0 && (UINT32)Px < gScreenW) {
            HalVideoDrawPixelRaw((UINT32)Px, Y, COLOR_WHITE);
        }
        if (Py >= 0 && (UINT32)Py < gScreenH) {
            HalVideoDrawPixelRaw(X, (UINT32)Py, COLOR_WHITE);
        }
    }
    HalVideoDrawPixelRaw(X, Y, COLOR_RED);
}

static void CursorRestore(void) {
    UINT32 Dy;
    UINT32 Dx;

    if (!gCursorVisible) {
        return;
    }
    for (Dy = 0; Dy < gSaveH; Dy++) {
        for (Dx = 0; Dx < gSaveW; Dx++) {
            HalVideoDrawPixelRaw(gSaveX + Dx, gSaveY + Dy,
                                 gUnder[Dy * gSaveW + Dx]);
        }
    }
    gCursorVisible = 0;
}

static void CursorPaint(void) {
    UINT32 Dy;
    UINT32 Dx;

    /* 已可见时禁止直接再画：否则 gUnder 会采到十字，Restore 后留下印记 */
    if (gCursorVisible) {
        CursorRestore();
    }
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
            GfxIrqEnter();
            CursorRestore();
            HalVideoPresent();
            GfxIrqLeave();
        }
        gCursorX = X;
        gCursorY = Y;
        return;
    }

    GfxIrqEnter();
    CursorRestore();
    gCursorX = X;
    gCursorY = Y;
    CursorPaint();
    HalVideoPresent();
    GfxIrqLeave();
}

static void DrawTitleStringOccluded(int Idx, const GUI_WINDOW *W) {
    const char *S;
    UINT32 X;
    UINT32 Y;

    if (W->Title == 0 || W->Title[0] == 0) {
        return;
    }
    S = W->Title;
    X = W->X + 8;
    Y = W->Y + 4;
    while (*S) {
        UINT32 Cp;
        UINTN N;
        UINT32 Adv;

        N = Utf8Decode(S, &Cp);
        if (N == 0) {
            S++;
            continue;
        }
        Adv = FontCodepointAdvance(Cp);
        if (Adv == 0) {
            Adv = 8;
        }
        if (!PixelOccludedByAbove(Idx, X, Y) &&
            !PixelOccludedByAbove(Idx, X + Adv / 2, Y)) {
            HalVideoDrawCodepointAt(X, Y, Cp, COLOR_WHITE);
        }
        X += Adv;
        S += N;
    }
}

static void DrawWindowAtEx(int Idx, int Occlude) {
    const GUI_WINDOW *W = &gWins[Idx];

    if (!W->Active) {
        return;
    }
    /* 标题在客户区外；若仍开着 Shell/Settings clip，DrawString 会被裁掉 */
    HalVideoClearClip();
    if (Occlude) {
        FillRectOccluded(Idx, W->X, W->Y, W->Width, TITLE_HEIGHT, TitleBarColor(Idx));
        DrawHLineOccluded(Idx, W->X, W->X + W->Width - 1, W->Y, COLOR_WHITE);
        DrawHLineOccluded(Idx, W->X, W->X + W->Width - 1, W->Y + W->Height - 1,
                          COLOR_WHITE);
        DrawVLineOccluded(Idx, W->X, W->Y, W->Y + W->Height - 1, COLOR_WHITE);
        DrawVLineOccluded(Idx, W->X + W->Width - 1, W->Y, W->Y + W->Height - 1,
                          COLOR_WHITE);
        if (W->Width > 2 && W->Height > TITLE_HEIGHT + 1) {
            FillRectOccluded(Idx, W->X + 1, W->Y + TITLE_HEIGHT, W->Width - 2,
                             W->Height - TITLE_HEIGHT - 1, W->Background);
        }
        DrawTitleStringOccluded(Idx, W);
        DrawCloseButton(Idx, W);
        return;
    }
    /*
     * 不透明整窗（主题自下而上合成用）：上层稍后覆盖，勿 Occlude，
     * 否则重叠区不画 → 标题镂空、客户区换色不全。
     */
    HalVideoFillRect(W->X, W->Y, W->Width, TITLE_HEIGHT, TitleBarColor(Idx));
    HalVideoFillRect(W->X, W->Y, W->Width, 1, COLOR_WHITE);
    HalVideoFillRect(W->X, W->Y + W->Height - 1, W->Width, 1, COLOR_WHITE);
    HalVideoFillRect(W->X, W->Y, 1, W->Height, COLOR_WHITE);
    HalVideoFillRect(W->X + W->Width - 1, W->Y, 1, W->Height, COLOR_WHITE);
    if (W->Width > 2 && W->Height > TITLE_HEIGHT + 1) {
        HalVideoFillRect(W->X + 1, W->Y + TITLE_HEIGHT, W->Width - 2,
                         W->Height - TITLE_HEIGHT - 1, W->Background);
    }
    if (W->Title != 0 && W->Title[0] != 0) {
        HalVideoDrawStringAt(W->X + 8, W->Y + 4, W->Title, COLOR_WHITE);
    }
    /* 关闭钮也整块画，勿 Occlude（否则未聚焦 Shell 的 × 可能缺块） */
    {
        UINT32 Bx;
        UINT32 By;
        UINT32 Bw;
        UINT32 Bh;
        UINT32 Pad;
        UINT32 I;
        UINT32 Span;

        CloseButtonRect(W, &Bx, &By, &Bw, &Bh);
        HalVideoFillRect(Bx, By, Bw, Bh, COLOR_RED);
        if (Bw >= 2 && Bh >= 2) {
            HalVideoFillRect(Bx, By, Bw, 1, COLOR_WHITE);
            HalVideoFillRect(Bx, By + Bh - 1, Bw, 1, COLOR_WHITE);
            HalVideoFillRect(Bx, By, 1, Bh, COLOR_WHITE);
            HalVideoFillRect(Bx + Bw - 1, By, 1, Bh, COLOR_WHITE);
        }
        Pad = 7;
        if (Bw > Pad * 2 + 2 && Bh > Pad * 2 + 2) {
            Span = Bw - 1 - Pad * 2;
            for (I = 0; I <= Span; I++) {
                HalVideoDrawPixelRaw(Bx + Pad + I, By + Pad + I, COLOR_WHITE);
                HalVideoDrawPixelRaw(Bx + Bw - 1 - Pad - I, By + Pad + I,
                                     COLOR_WHITE);
            }
        }
    }
}

static void DrawWindowAt(int Idx) {
    DrawWindowAtEx(Idx, 1);
}

/* 仅重绘标题栏与边框，保留客户区已有文字；不画到上层窗口上 */
static void DrawWindowChromeAt(int Idx) {
    const GUI_WINDOW *W = &gWins[Idx];

    if (!W->Active) {
        return;
    }
    HalVideoClearClip();
    FillRectOccluded(Idx, W->X, W->Y, W->Width, TITLE_HEIGHT, TitleBarColor(Idx));
    DrawHLineOccluded(Idx, W->X, W->X + W->Width - 1, W->Y, COLOR_WHITE);
    DrawHLineOccluded(Idx, W->X, W->X + W->Width - 1, W->Y + W->Height - 1,
                      COLOR_WHITE);
    DrawVLineOccluded(Idx, W->X, W->Y, W->Y + W->Height - 1, COLOR_WHITE);
    DrawVLineOccluded(Idx, W->X + W->Width - 1, W->Y, W->Y + W->Height - 1,
                      COLOR_WHITE);
    DrawTitleStringOccluded(Idx, W);
    DrawCloseButton(Idx, W);
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
                                  ThemeDesktopBg());
                }
                InRun = 0;
            }
        }
        if (InRun && X + W > RunStart) {
            HalVideoFillRect(RunStart, Row, X + W - RunStart, 1, ThemeDesktopBg());
        }
    }
}

static void ResetDragState(void) {
    /*
     * 只清拖动合成用的 snap/under/dirty。
     * 绝不能清 gWinBackupValid：单击标题栏也会进 GuiDragEnd，若无重叠会走这里，
     * 清掉后备份后 SyncWindowVisuals 只能 DrawWindowAt，Settings/Shell 文字全没。
     */
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

/*
 * ForceFull：主题自下而上刚画完本窗、上层尚未覆盖时，必须整窗 ReadRect，
 * 否则 Occluded 路径会跳过重叠区，备份镂空，透视桌面/抬窗花屏。
 */
static void BackupWindowAtEx(int Idx, int ForceFull) {
    const GUI_WINDOW *Win = &gWins[Idx];
    UINT32 Rw;
    UINT32 Rh;
    UINT32 Bw;
    int HadValid;
    UINT32 *OldBuf;
    UINT32 Row;
    UINT32 Col;
    int WasVisible;

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

    HadValid = gWinBackupValid[Idx];
    OldBuf = gWinBackup[Idx];
    if (!EnsureWindowBackupBuf(Idx)) {
        gWinBackupValid[Idx] = 0;
        return;
    }
    if (gWinBackup[Idx] != OldBuf) {
        HadValid = 0;
    }

    WasVisible = gCursorVisible;
    ComposeBegin();
    GfxIrqEnter();
    CursorRestore();
    GfxIrqLeave();

    if (!ForceFull && WindowOccludedByOther(Idx)) {
        if (!HadValid) {
            gWinBackupValid[Idx] = 0;
            if (WasVisible) {
                GfxIrqEnter();
                CursorPaint();
                GfxIrqLeave();
            }
            ComposeEnd();
            return;
        }
        Bw = gWinBackupW[Idx];
        if (Bw == 0) {
            Bw = Rw;
        }
        for (Row = 0; Row < Rh; Row++) {
            for (Col = 0; Col < Rw; Col++) {
                UINT32 Px = Win->X + Col;
                UINT32 Py = Win->Y + Row;

                if (PixelCoveredByHigherWindow(Idx, Px, Py)) {
                    continue;
                }
                if (Row < gWinBackupH[Idx] && Col < Bw) {
                    gWinBackup[Idx][Row * Bw + Col] = HalVideoReadPixel(Px, Py);
                }
            }
        }
        gWinBackupW[Idx] = Rw;
        gWinBackupH[Idx] = Rh;
        gWinBackupValid[Idx] = 1;
        if (WasVisible) {
            GfxIrqEnter();
            CursorPaint();
            GfxIrqLeave();
        }
        ComposeEnd();
        return;
    }

    HalVideoReadRect(Win->X, Win->Y, Rw, Rh, gWinBackup[Idx]);
    gWinBackupW[Idx] = Rw;
    gWinBackupH[Idx] = Rh;
    gWinBackupValid[Idx] = 1;
    if (WasVisible) {
        GfxIrqEnter();
        CursorPaint();
        GfxIrqLeave();
    }
    ComposeEnd();
}

static void BackupWindowAt(int Idx) {
    BackupWindowAtEx(Idx, 0);
}

/* 与 DrawWindowAt 布局一致；仅作无备份时的回退 */
static UINT32 AnalyticWindowPixel(int Idx, UINT32 Px, UINT32 Py) {
    const GUI_WINDOW *W = &gWins[Idx];
    UINT32 Lx;
    UINT32 Ly;

    if (!W->Active) {
        return ThemeDesktopBg();
    }
    if (Px < W->X || Py < W->Y || Px >= W->X + W->Width || Py >= W->Y + W->Height) {
        return ThemeDesktopBg();
    }
    Lx = Px - W->X;
    Ly = Py - W->Y;
    if (Ly < TITLE_HEIGHT) {
        return TitleBarColor(Idx);
    }
    if (Ly == W->Height - 1 || Lx == 0 || Lx == W->Width - 1) {
        return COLOR_WHITE;
    }
    /* 与 DrawWindowAt 一致：白边内侧整片客户区底色 */
    return W->Background;
}

/*
 * 被拖窗下方可见像素：优先窗备份（含 Shell 文字）；无备份再解析空壳。
 * 注意：若备份是在被上层盖住时从 FB 抓的，重叠区可能脏——Capture 时用重绘采样避免。
 */
static UINT32 TopmostBelowDragPixel(UINT32 Px, UINT32 Py, int DragIdx) {
    int i;
    UINT32 IconColor;

    for (i = DragIdx - 1; i >= 0; i--) {
        if (!gWins[i].Active) {
            continue;
        }
        if (Px >= gWins[i].X && Py >= gWins[i].Y &&
            Px < gWins[i].X + gWins[i].Width &&
            Py < gWins[i].Y + gWins[i].Height) {
            if (gWinBackupValid[i] && gWinBackup[i] != 0) {
                return SampleWindowBackupPixel(i, Px, Py);
            }
            return AnalyticWindowPixel(i, Px, Py);
        }
    }
    if (DesktopSamplePixel(Px, Py, &IconColor)) {
        return IconColor;
    }
    return ThemeDesktopBg();
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
    int i;

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
    /* 含被拖窗的全屏快照：非起始 footprint 露底时用 */
    HalVideoReadRect(0, 0, gScreenW, gScreenH, gScreenSnap);
    gScreenSnapValid = 1;
    EnsureDragDirtyBuf(gScreenW, gScreenH);

    if (!EnsureUnderDragBuf(gDragStartW, gDragStartH)) {
        return;
    }

    /*
     * 起始 footprint 的「去被拖窗」场景：暂时取消被拖窗 Active，重画桌面+其它窗，
     * 再读入 under-drag，最后恢复 Active 并贴回被拖窗。
     */
    HalVideoClearClip();
    gWins[DragIdx].Active = 0;
    UiFillRectangle(gDragStartX, gDragStartY, gDragStartW, gDragStartH,
                    ThemeDesktopBg());
    DesktopDrawRect(gDragStartX, gDragStartY, gDragStartW, gDragStartH);
    for (i = 0; i < MAX_WINS; i++) {
        if (!gWins[i].Active || i == DragIdx) {
            continue;
        }
        if (RectIntersects(gWins[i].X, gWins[i].Y, gWins[i].Width, gWins[i].Height,
                           gDragStartX, gDragStartY, gDragStartW, gDragStartH)) {
            /* 必须贴备份（含客户区文字），禁止 DrawWindowAt 空壳 */
            PaintWindowFromBackup(i);
        }
    }
    HalVideoReadRect(gDragStartX, gDragStartY, gDragStartW, gDragStartH, gUnderDrag);
    gUnderDragValid = 1;
    gWins[DragIdx].Active = 1;

    /* 贴回被拖窗（备份应在 Begin/Start 里已抓好） */
    if (gWinBackupValid[DragIdx] && gWinBackup[DragIdx] != 0) {
        HalVideoWriteRect(gDragStartX, gDragStartY, gWinBackupW[DragIdx],
                          gWinBackupH[DragIdx], gWinBackup[DragIdx]);
        DrawWindowChromeAt(DragIdx);
    } else {
        DrawWindowAt(DragIdx);
    }

    /* 相交窗已从备份恢复，无需再 BackupWindowAt（以免读到瞬时脏 FB） */
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
    /* G7：抓屏/合成不关中断，只锁光标擦除；ComposeBusy 防嵌套鼠标 */
    ComposeBegin();
    GfxIrqEnter();
    CursorRestore();
    HalVideoPresent();
    GfxIrqLeave();
    GuiFocusSave();
    if (gDragHasBackup && AllActiveWindowsHaveValidBackup()) {
        int i;

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
    ComposeEnd();
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
        return ThemeDesktopBg();
    }
    Bw = gWinBackupW[Idx];
    Bh = gWinBackupH[Idx];
    if (Px < W->X || Py < W->Y) {
        return ThemeDesktopBg();
    }
    Lx = Px - W->X;
    Ly = Py - W->Y;
    if (Lx >= Bw || Ly >= Bh) {
        return ThemeDesktopBg();
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
    return TopmostBelowDragPixel(Px, Py, DragIdx);
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
        /* Present：短临界区写后缓冲再提交到 GOP（PR-G9） */
        GfxIrqEnter();
        HalVideoWriteRect(DuX, DuY, DuW, DuH, gDragDirty);
        HalVideoPresent();
        GfxIrqLeave();
        return;
    }
    /* 离屏缓冲不足：按 DRAG_ROW_MAX 横向分块写，禁止静默截断右侧 */
    {
        UINT32 Col0;

        for (Col0 = 0; Col0 < DuW; Col0 += DRAG_ROW_MAX) {
            UINT32 ChunkW = DuW - Col0;

            if (ChunkW > DRAG_ROW_MAX) {
                ChunkW = DRAG_ROW_MAX;
            }
            for (Row = 0; Row < DuH; Row++) {
                UINT32 Py = DuY + Row;
                UINT32 Col;

                for (Col = 0; Col < ChunkW; Col++) {
                    UINT32 Px = DuX + Col0 + Col;

                    gDragRowBuf[Col] =
                        CompositeDragPixel(Px, Py, DragIdx, Nx, Ny, Ww, Wh);
                }
                HalVideoWriteRect(DuX + Col0, Py, ChunkW, 1, gDragRowBuf);
            }
        }
        GfxIrqEnter();
        HalVideoPresent();
        GfxIrqLeave();
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

/* 清除整片旧 footprint：先恢复被盖住的其它窗，再填桌面色与图标（避让窗口） */
static void ClearOldDragFootprint(UINT32 Ox, UINT32 Oy, UINT32 Ww, UINT32 Wh,
                                  int DragIdx) {
    RestoreWindowsInFootprint(Ox, Oy, Ww, Wh, DragIdx);
    FillDesktopRectClipped(Ox, Oy, Ww, Wh);
    DesktopDrawRect(Ox, Oy, Ww, Wh);
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

    /* G7：合成开中断；仅光标 erase/paint 与 Present 进 GfxIrq */
    ComposeBegin();
    GfxIrqEnter();
    if (gCursorVisible) {
        CursorRestore();
        HalVideoPresent();
    }
    GfxIrqLeave();
    if (gDragHasBackup) {
        W->X = NewX;
        W->Y = NewY;
        RedrawDragFrame(Idx, Ox, Oy);
        /* RedrawDragFrame / Composite 路径内已 Present */
    } else if (gDragWin >= 0) {
        W->X = NewX;
        W->Y = NewY;
        HalVideoClearClip();
        if (gWinBackupValid[Idx]) {
            CompositeDragDirtyRegion(Idx, Ox, Oy, Ww, Wh);
        } else {
            ClearOldDragFootprint(Ox, Oy, Ww, Wh, Idx);
            PaintAllWindowsDraw(Idx);
            GfxIrqEnter();
            HalVideoPresent();
            GfxIrqLeave();
        }
    } else {
        HalVideoCopyRect(Ox, Oy, NewX, NewY, Ww, Wh);
        W->X = NewX;
        W->Y = NewY;
        ClearOldDragFootprint(Ox, Oy, Ww, Wh, Idx);
        RefreshOtherChrome(Idx);
        DrawWindowChromeAt(Idx);
    }
    if (gDragWin < 0) {
        GfxIrqEnter();
        CursorPaint();
        HalVideoPresent();
        GfxIrqLeave();
    }
    ComposeEnd();
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
    /* 与 DrawWindowAt 一致：1px 白边内侧再加 GUI_CLIENT_PAD 文本边距 */
    if (X) {
        *X = Win->X + 1 + Pad;
    }
    if (Y) {
        *Y = Win->Y + TITLE_HEIGHT + Pad;
    }
    if (Width) {
        *Width = (Win->Width > 2 + Pad * 2) ? Win->Width - 2 - Pad * 2 : 0;
    }
    if (Height) {
        *Height = (Win->Height > TITLE_HEIGHT + 1 + Pad * 2) ?
             Win->Height - TITLE_HEIGHT - 1 - Pad * 2 : 0;
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
    UINT32 Cx;
    UINT32 Cy;
    UINT32 W;
    UINT32 H;
    UINT32 Bg;
    UINT32 Ax;
    UINT32 Ay;
    GUI_WINDOW *Win;

    if (gFocusWin < 0 || gFocusWin >= MAX_WINS || !gWins[gFocusWin].Active) {
        return;
    }
    ConsoleFocusSave();
    Win = &gWins[gFocusWin];
    if (!GuiFocusClient(&Cx, &Cy, &W, &H, &Bg)) {
        return;
    }
    HalVideoGetTextCursor(&Ax, &Ay);
    Win->TermX = (Ax >= Cx) ? (Ax - Cx) : 0;
    Win->TermY = (Ay >= Cy) ? (Ay - Cy) : 0;
    if (W > 0 && Win->TermX >= W) {
        Win->TermX = W - 1;
    }
    if (H > 0 && Win->TermY >= H) {
        Win->TermY = H - 1;
    }
    Win->TermSet = 1;
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

void GuiShellRequestPrompt(void) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (gWins[i].Active && gWins[i].Kind == GUI_WIN_SHELL) {
            gWins[i].PromptShown = 0;
        }
    }
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
        Win->TermX = 0;
        Win->TermY = 0;
        return;
    }
    Tx = X + Win->TermX;
    Ty = Y + Win->TermY;
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
    Win->TermX = Tx - X;
    Win->TermY = Ty - Y;
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
    UINT32 Cx;
    UINT32 Cy;
    UINT32 W;
    UINT32 H;
    UINT32 Bg;
    UINT32 Ax;
    UINT32 Ay;
    GUI_WINDOW *Win;

    if (gFocusWin < 0 || gFocusWin >= MAX_WINS || !gWins[gFocusWin].Active) {
        return;
    }
    if (!GuiFocusClient(&Cx, &Cy, &W, &H, &Bg)) {
        return;
    }
    Win = &gWins[gFocusWin];
    HalVideoGetTextCursor(&Ax, &Ay);
    Win->TermX = (Ax >= Cx) ? (Ax - Cx) : 0;
    Win->TermY = (Ay >= Cy) ? (Ay - Cy) : 0;
    if (W > 0 && Win->TermX >= W) {
        Win->TermX = W - 1;
    }
    if (H > 0 && Win->TermY >= H) {
        Win->TermY = H - 1;
    }
    Win->TermSet = 1;
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
    /*
     * 整块清客户区。主题合成靠 gDeferPresent + 上层后画，勿 FillRectOccluded，
     * 否则重叠区不换色，抬窗后备份镂空。
     */
    HalVideoFillRect(X, Y, W, H, Bg);
    HalVideoSetClipOrigin(X, Y, W, H, Bg);
    Win = &gWins[gFocusWin];
    Win->TermX = 0;
    Win->TermY = 0;
    Win->TermSet = 1;
    GuiBackupSyncRect(X, Y, W, H);
    CursorPaint();
    GfxPresent();
    GfxIrqLeave();
}

int GuiShellAcceptsInput(void) {
    return gFocusWin >= 0 && gFocusWin < MAX_WINS &&
           gWins[gFocusWin].Active &&
           gWins[gFocusWin].Kind == GUI_WIN_SHELL &&
           !WindowOccludedByOther(gFocusWin);
}

int GuiShellWindowActive(int Idx) {
    return Idx >= 0 && Idx < MAX_WINS && gWins[Idx].Active &&
           gWins[Idx].Kind == GUI_WIN_SHELL;
}

void GuiSetFocusWin(int Idx) {
    if (Idx >= 0 && Idx < MAX_WINS && gWins[Idx].Active) {
        gFocusWin = Idx;
    }
}

void GuiBackupFocusWindow(void) {
    if (gFocusWin >= 0 && gFocusWin < MAX_WINS && gWins[gFocusWin].Active) {
        BackupWindowAt(gFocusWin);
    }
}

/* 置顶并按备份重合成，避免只改焦点却在下层写穿 */
void GuiRaiseToFront(int Idx) {
    if (Idx < 0 || Idx >= MAX_WINS || !gWins[Idx].Active) {
        return;
    }
    RaiseWindow(Idx);
    SyncWindowVisuals();
    GuiFocusApply();
    /* 顶层无有效备份时补内容，再抓一份干净备份 */
    if (gWins[Idx].Kind == GUI_WIN_SETTINGS) {
        SettingsUiRepaint();
        BackupWindowAt(Idx);
    } else if (gWins[Idx].Kind == GUI_WIN_FILES) {
        FilesUiRepaint();
        BackupWindowAt(Idx);
    } else if (gWins[Idx].Kind == GUI_WIN_SHELL && !gWinBackupValid[Idx]) {
        /* 欢迎语级恢复；完整历史需备份一直有效 */
        ConsoleOnShellOpened();
        BackupWindowAt(Idx);
    }
}

int GuiFocusIndex(void) {
    return gFocusWin;
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
        Win->TermX = 0;
        Win->TermY = 0;
        Win->TermSet = 1;
    }
}

void GuiRedraw(void) {
    int i;

    /* G7：桌面/窗体开中断绘制；ComposeBusy 丢弃嵌套鼠标；只锁光标 */
    ComposeBegin();
    GfxIrqEnter();
    CursorRestore();
    GfxIrqLeave();
    HalVideoClearClip();
    UiFillRectangle(0, 0, gScreenW, gScreenH, ThemeDesktopBg());
    DesktopDraw();
    for (i = 0; i < MAX_WINS; i++) {
        DrawWindowAt(i);
    }
    GfxIrqEnter();
    CursorPaint();
    HalVideoPresent();
    GfxIrqLeave();
    ComposeEnd();
}

void GuiApplyThemeColors(void) {
    int i;
    UINT32 Bg = ThemeShellClientBg();

    /* 只更新属性；整屏提交见 GuiComposeThemeScene（PR-G8） */
    for (i = 0; i < MAX_WINS; i++) {
        if (gWins[i].Active && gWins[i].Kind == GUI_WIN_SHELL) {
            gWins[i].Background = Bg;
            gWins[i].TermSet = 0;
            gWins[i].TermX = 0;
            gWins[i].TermY = 0;
            gWins[i].InputLen = 0;
            gWins[i].InputLine[0] = 0;
            gWins[i].PromptShown = 0;
            gWins[i].WaitPrompt = 0;
        } else if (gWins[i].Active && gWins[i].Kind == GUI_WIN_SETTINGS) {
            gWins[i].Background = ThemeSettingsClientBg();
        } else if (gWins[i].Active && gWins[i].Kind == GUI_WIN_FILES) {
            gWins[i].Background = ThemeSettingsClientBg();
        }
    }
}

/*
 * PR-G8/G9：主题一次合成（painter's algorithm，后缓冲上完成再 Present）：
 * 1) 整屏桌面 + 图标；2) 自下而上不透明整窗 + 内容；每窗立刻 ForceFull 备份；
 * gDeferPresent 避免中间态刷到 GOP（灰闪 / 下层盖上层）。
 */
void GuiComposeThemeScene(void) {
    int i;
    int SavedFocus = gFocusWin;

    gDeferPresent = 1;
    ComposeBegin();
    GfxIrqEnter();
    CursorRestore();
    GfxIrqLeave();
    HalVideoClearClip();

    /* 先铺底：有 DeferPresent 时整屏 wipe 不会露到屏幕 */
    UiFillRectangle(0, 0, gScreenW, gScreenH, ThemeDesktopBg());
    DesktopDraw();

    for (i = 0; i < MAX_WINS; i++) {
        if (!gWins[i].Active) {
            continue;
        }
        /*
         * 标题栏颜色看 gFocusWin。画 Shell 内容时会暂把焦点设到该窗；
         * 若不先恢复 SavedFocus，后画的 Settings 标题会被画成灰色（失焦）。
         */
        gFocusWin = SavedFocus;
        DrawWindowAtEx(i, 0);
        if (gWins[i].Kind == GUI_WIN_SHELL) {
            gFocusWin = i;
            ConsolePaintShellWindow(i);
        } else if (gWins[i].Kind == GUI_WIN_SETTINGS) {
            gFocusWin = i;
            SettingsUiPaintFocused();
        } else if (gWins[i].Kind == GUI_WIN_FILES) {
            gFocusWin = i;
            FilesUiPaintFocused();
        }
        /* 上层尚未画上：整窗备份，避免重叠区镂空透视 */
        BackupWindowAtEx(i, 1);
    }

    gFocusWin = SavedFocus;
    if (SavedFocus >= 0 && SavedFocus < MAX_WINS && gWins[SavedFocus].Active) {
        GuiFocusApply();
    } else {
        HalVideoClearClip();
    }
    GfxIrqEnter();
    CursorPaint();
    GfxIrqLeave();
    ComposeEnd();
    gDeferPresent = 0;
    GfxIrqEnter();
    HalVideoPresent();
    GfxIrqLeave();
}

void GuiPaintWindow(int Idx) {
    if (Idx < 0 || Idx >= MAX_WINS || !gWins[Idx].Active) {
        return;
    }
    GfxIrqEnter();
    CursorRestore();
    DrawWindowAt(Idx);
    CursorPaint();
    HalVideoPresent();
    GfxIrqLeave();
}

void GuiBackupAllWindows(void) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (gWins[i].Active) {
            BackupWindowAt(i);
        }
    }
}

int GuiPointInAnyWindow(UINT32 X, UINT32 Y) {
    return PointInAnyActiveWindow(X, Y);
}

static int AllocWindowSlot(void) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (!gWins[i].Active) {
            return i;
        }
    }
    return -1;
}

static void PlaceNewWindow(int Idx, UINT32 *OutX, UINT32 *OutY,
                           UINT32 *OutW, UINT32 *OutH) {
    UINT32 Margin = 48;
    UINT32 Cascade = (UINT32)Idx * 28;
    UINT32 W;
    UINT32 H;

    W = gScreenW > Margin * 2 + 200 ? gScreenW - Margin * 2 : gScreenW - 32;
    H = gScreenH > Margin * 2 + 120 ? gScreenH - Margin * 2 : gScreenH - 32;
    if (W > 720) {
        W = 720;
    }
    if (H > 480) {
        H = 480;
    }
    *OutX = Margin + Cascade;
    *OutY = Margin + Cascade;
    if (*OutX + W > gScreenW) {
        *OutX = Margin;
    }
    if (*OutY + H > gScreenH) {
        *OutY = Margin;
    }
    *OutW = W;
    *OutH = H;
}

int GuiOpenShell(void) {
    int Idx;
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;

    Idx = AllocWindowSlot();
    if (Idx < 0) {
        return -1;
    }
    PlaceNewWindow(Idx, &X, &Y, &W, &H);
    gWins[Idx].Active = 1;
    gWins[Idx].Kind = GUI_WIN_SHELL;
    gWins[Idx].X = X;
    gWins[Idx].Y = Y;
    gWins[Idx].Width = W;
    gWins[Idx].Height = H;
    gWins[Idx].Background = ThemeShellClientBg();
    gWins[Idx].Title = LocStr(MSG_APP_SHELL);
    gWins[Idx].TermSet = 0;
    gWins[Idx].InputLen = 0;
    gWins[Idx].WaitPrompt = 0;
    /* 先标已 prompt，避免 FocusApply→FocusLoad 抢画；OnShellOpened 再清客户区重画 */
    gWins[Idx].PromptShown = 1;
    gWins[Idx].InputLine[0] = 0;

    /* M3/G7：备份前必擦光标，避免十字烙进窗备份 */
    ComposeBegin();
    GfxIrqEnter();
    CursorRestore();
    GfxIrqLeave();
    HalVideoClearClip();
    DrawWindowAt(Idx);
    BackupWindowAt(Idx);
    ComposeEnd();
    GuiFocusSave();
    RaiseWindow(Idx);
    SyncWindowVisuals();
    GuiFocusApply();
    DebugWrite("gui: open shell idx=");
    DebugHex32((UINT32)gFocusWin);
    DebugWrite("\n");
    return gFocusWin;
}

int GuiOpenSettings(void) {
    int Idx;
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Margin = 48;

    Idx = AllocWindowSlot();
    if (Idx < 0) {
        return -1;
    }
    /* 靠右放置；高度按字体行距预留，避免菜单画出窗外叠在 Shell/桌面上 */
    W = 560;
    {
        UINT32 LineH = FontAdvanceY();
        UINT32 NeedH;

        if (LineH < 16) {
            LineH = 16;
        }
        /* 标题 + 边距 + Display 页约 14 行（含 Now/提示） */
        NeedH = TITLE_HEIGHT + GUI_CLIENT_PAD * 2 + 12 + LineH * 14 + 8;
        H = NeedH;
        if (H < 420) {
            H = 420;
        }
    }
    if (W + Margin * 2 > gScreenW) {
        W = gScreenW > Margin * 2 ? gScreenW - Margin * 2 : gScreenW / 2;
    }
    if (H + Margin * 2 > gScreenH) {
        H = gScreenH > Margin * 2 ? gScreenH - Margin * 2 : gScreenH / 2;
    }
    X = (gScreenW > W + Margin) ? (gScreenW - W - Margin) : Margin;
    Y = Margin;
    gWins[Idx].Active = 1;
    gWins[Idx].Kind = GUI_WIN_SETTINGS;
    gWins[Idx].X = X;
    gWins[Idx].Y = Y;
    gWins[Idx].Width = W;
    gWins[Idx].Height = H;
    gWins[Idx].Background = ThemeSettingsClientBg();
    gWins[Idx].Title = LocStr(MSG_APP_SETTINGS);
    gWins[Idx].TermSet = 0;
    gWins[Idx].InputLen = 0;
    gWins[Idx].WaitPrompt = 0;
    gWins[Idx].PromptShown = 0;
    gWins[Idx].InputLine[0] = 0;

    ComposeBegin();
    GfxIrqEnter();
    CursorRestore();
    GfxIrqLeave();
    HalVideoClearClip();
    DrawWindowAt(Idx);
    ComposeEnd();
    gFocusWin = Idx;
    RaiseWindow(Idx);
    SyncWindowVisuals();
    SettingsUiOpen();
    BackupWindowAt(Idx);
    GuiFocusApply();
    BackupWindowAt(gFocusWin);
    DebugWrite("gui: open settings idx=");
    DebugHex32((UINT32)gFocusWin);
    DebugWrite("\n");
    return gFocusWin;
}

int GuiOpenFiles(void) {
    int Idx;
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Margin = 40;

    Idx = AllocWindowSlot();
    if (Idx < 0) {
        return -1;
    }
    W = 640;
    H = 480;
    if (W + Margin * 2 > gScreenW) {
        W = gScreenW > Margin * 2 ? gScreenW - Margin * 2 : gScreenW / 2;
    }
    if (H + Margin * 2 > gScreenH) {
        H = gScreenH > Margin * 2 ? gScreenH - Margin * 2 : gScreenH / 2;
    }
    X = Margin;
    Y = (gScreenH > H + Margin) ? (gScreenH - H - Margin) : Margin;
    gWins[Idx].Active = 1;
    gWins[Idx].Kind = GUI_WIN_FILES;
    gWins[Idx].X = X;
    gWins[Idx].Y = Y;
    gWins[Idx].Width = W;
    gWins[Idx].Height = H;
    gWins[Idx].Background = ThemeSettingsClientBg();
    gWins[Idx].Title = LocStr(MSG_APP_FILES);
    gWins[Idx].TermSet = 0;
    gWins[Idx].InputLen = 0;
    gWins[Idx].WaitPrompt = 0;
    gWins[Idx].PromptShown = 0;
    gWins[Idx].InputLine[0] = 0;

    ComposeBegin();
    GfxIrqEnter();
    CursorRestore();
    GfxIrqLeave();
    HalVideoClearClip();
    DrawWindowAt(Idx);
    ComposeEnd();
    gFocusWin = Idx;
    RaiseWindow(Idx);
    SyncWindowVisuals();
    FilesUiOpen();
    BackupWindowAt(Idx);
    GuiFocusApply();
    BackupWindowAt(gFocusWin);
    DebugWrite("gui: open files idx=");
    DebugHex32((UINT32)gFocusWin);
    DebugWrite("\n");
    return gFocusWin;
}

GUI_WIN_KIND GuiWindowKind(int Idx) {
    if (Idx < 0 || Idx >= MAX_WINS || !gWins[Idx].Active) {
        return GUI_WIN_NONE;
    }
    return gWins[Idx].Kind;
}

GUI_WIN_KIND GuiFocusKind(void) {
    return GuiWindowKind(gFocusWin);
}

void GuiRefreshTitles(void) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (!gWins[i].Active) {
            continue;
        }
        if (gWins[i].Kind == GUI_WIN_SHELL) {
            gWins[i].Title = LocStr(MSG_APP_SHELL);
        } else if (gWins[i].Kind == GUI_WIN_SETTINGS) {
            gWins[i].Title = LocStr(MSG_APP_SETTINGS);
        } else if (gWins[i].Kind == GUI_WIN_FILES) {
            gWins[i].Title = LocStr(MSG_APP_FILES);
        }
    }
    ComposeBegin();
    GfxIrqEnter();
    CursorRestore();
    GfxIrqLeave();
    SyncWindowVisualsEx(1);
    ComposeEnd();
    if (GuiFocusKind() == GUI_WIN_SETTINGS) {
        SettingsUiRepaint();
    } else if (GuiFocusKind() == GUI_WIN_FILES) {
        FilesUiRepaint();
    }
}

void GuiInit(void) {
    HalVideoGetSize(&gScreenW, &gScreenH);
    if (gScreenW == 0) {
        gScreenW = 1024;
        gScreenH = 768;
    }
    gCursorX = gScreenW / 2;
    gCursorY = gScreenH / 2;
    gFocusWin = -1;
    gCursorVisible = 0;
    gDragWin = -1;

    {
        int i;

        for (i = 0; i < MAX_WINS; i++) {
            gWins[i].Active = 0;
            gWins[i].Kind = GUI_WIN_NONE;
            gWins[i].TermSet = 0;
            gWins[i].InputLen = 0;
            gWins[i].WaitPrompt = 0;
            gWins[i].PromptShown = 0;
            gWins[i].InputLine[0] = 0;
            gWins[i].Title = "";
            gWins[i].Background = ThemeShellClientBg();
        }
    }

    PreallocWindowBackups();
    DesktopInit();
    GuiRedraw();
    DebugWrite("gui: desktop ready (icons + no app windows)\n");
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
            GfxIrqEnter();
            CursorRestore();
            GfxIrqLeave();
            RaiseWindow(gFocusWin);
            gDragWin = gFocusWin;
            gDragOffX = (INT32)X - (INT32)gWins[gFocusWin].X;
            gDragOffY = (INT32)Y - (INT32)gWins[gFocusWin].Y;
            gDragArmed = 1;
        }
        if (GuiFocusKind() == GUI_WIN_SETTINGS) {
            SettingsUiRepaint();
        } else if (GuiFocusKind() == GUI_WIN_FILES) {
            if (PointInTitle(&gWins[gFocusWin], X, Y)) {
                FilesUiRepaint();
            } else {
                FilesUiOnClick(X, Y);
            }
        } else if (GuiFocusKind() == GUI_WIN_SHELL &&
                   !gWinBackupValid[gFocusWin]) {
            ConsoleOnShellOpened();
        } else if (gFocusWin >= 0) {
            BackupWindowAt(gFocusWin);
        }
        return 1;
    }
    /* 未点中窗口：桌面图标（双击打开） */
    return DesktopHandleClick(X, Y);
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
    if (gDragArmed) {
        StartDragBackups(gDragWin);
        gDragArmed = 0;
    }
    MoveWindowTo(gDragWin, (UINT32)Nx, (UINT32)Ny);
}

static void GuiDragEnd(void) {
    int DragIdx = gDragWin;
    int DidDrag = gDragHasBackup;

    gDragWin = -1;
    gDragArmed = 0;
    if (DragIdx >= 0 && gWins[DragIdx].Active) {
        if (DidDrag) {
            INT32 Nx = (INT32)gCursorX - gDragOffX;
            INT32 Ny = (INT32)gCursorY - gDragOffY;

            ClampWindowPos(&gWins[DragIdx], &Nx, &Ny);
            MoveWindowTo(DragIdx, (UINT32)Nx, (UINT32)Ny);
            RaiseWindow(DragIdx);
            /*
             * 残影：拖动路径上旧 chrome 落在「当前窗矩形之外」，只贴窗擦不掉。
             * 先铺桌面再按备份贴回，清轨迹；空色块若已烙进备份则随后 Settings/Shell 重画补。
             */
            SyncWindowVisualsEx(1);
            GuiFocusApply();
        }
        if (gWins[DragIdx].Kind == GUI_WIN_SETTINGS) {
            SettingsUiRepaint();
        } else if (gWins[DragIdx].Kind == GUI_WIN_FILES) {
            FilesUiRepaint();
        } else if (gWins[DragIdx].Kind == GUI_WIN_SHELL &&
                   !gWinBackupValid[DragIdx]) {
            ConsoleOnShellOpened();
        }
        /* 清桌面合成后：无备份的 Shell 客户区是空壳，补画控制台 */
        if (DidDrag) {
            int i;

            for (i = 0; i < MAX_WINS; i++) {
                if (gWins[i].Active && gWins[i].Kind == GUI_WIN_SHELL &&
                    !gWinBackupValid[i]) {
                    int Prev = gFocusWin;

                    gFocusWin = i;
                    ConsoleOnShellOpened();
                    gFocusWin = Prev;
                }
            }
        }
        if (gWins[DragIdx].Active) {
            BackupWindowAt(DragIdx);
        }
    }
    if (!AnyWindowsOverlap()) {
        ResetDragState();
    }
}

void GuiPointerMove(UINT32 X, UINT32 Y) {
    CursorMove(X, Y);
    if (gDragWin >= 0 && (gCursorBtn & 1)) {
        GuiDragUpdate(X, Y);
    } else if (GuiFocusKind() == GUI_WIN_FILES) {
        /* PR-G11：列表悬停行（不拖动时） */
        FilesUiOnHover(X, Y);
    }
}

/* 帧缓冲绘制前：关中断并擦掉光标（避免 save-under 采到十字像素） */
void GuiFrameBufferBegin(void) {
    GfxIrqEnter();
    CursorRestore();
}

/* 帧缓冲绘制后：重画光标、Present；仅恢复进入 Begin 前已开启的中断 */
void GuiFrameBufferEnd(void) {
    CursorPaint();
    GfxPresent();
    GfxIrqLeave();
}

void GuiCursorPaint(void) {
    GfxIrqEnter();
    CursorRestore();
    CursorPaint();
    GfxPresent();
    GfxIrqLeave();
}

void GuiCursorHide(void) {
    GfxIrqEnter();
    CursorRestore();
    GfxPresent();
    GfxIrqLeave();
}

void GuiCursorShow(void) {
    GfxIrqEnter();
    CursorPaint();
    GfxPresent();
    GfxIrqLeave();
}

void GuiOnMouse(const GUI_MOUSE_STATE *Mouse) {
    static UINT8 PrevBtn;

    /* 合成进行中只跟踪坐标/钮，避免嵌套 Move/Capture 采到半成品 FB。
     * 若光标仍画在旧位置，先擦掉，否则 Compose 期间移动会留下十字印。 */
    if (gComposeBusy) {
        if (gCursorVisible &&
            (Mouse->X != gCursorX || Mouse->Y != gCursorY)) {
            GfxIrqEnter();
            CursorRestore();
            HalVideoPresent();
            GfxIrqLeave();
        }
        gCursorX = Mouse->X;
        gCursorY = Mouse->Y;
        gCursorBtn = Mouse->Buttons;
        return;
    }

    gCursorBtn = Mouse->Buttons;
    GuiPointerMove(Mouse->X, Mouse->Y);

    if ((Mouse->Buttons & 1) && !(PrevBtn & 1)) {
        GuiHandleClick(gCursorX, gCursorY);
    } else if ((Mouse->Buttons & 1) && gDragWin >= 0) {
        /* PR-G10 L2：与 GuiPollMouse 统一，按住拖动时持续更新 */
        GuiDragUpdate(gCursorX, gCursorY);
    }
    if (!(Mouse->Buttons & 1) && (PrevBtn & 1)) {
        GuiDragEnd();
    }
    PrevBtn = Mouse->Buttons;
}

/* 从 XHCI 鼠标队列取报告并交给 GuiOnMouse（单一边沿/拖动逻辑） */
void GuiPollMouse(void) {
    HAL_MOUSE_REPORT Raw;
    UINT32 Sw;
    UINT32 Sh;
    GUI_MOUSE_STATE M;

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

        M.X = X;
        M.Y = Y;
        M.Buttons = Raw.Buttons;
        GuiOnMouse(&M);
    }
}
