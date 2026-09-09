/*
 * GuiCompose.c — PR-R2：窗口合成 / 备份 / 主题场景
 */
#include "GuiPriv.h"
#include "UI.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Debug.h"
#include "PhysicalMemory.h"
#include "Theme.h"
#include "Font.h"
#include "Desktop.h"
#include "SettingsUi.h"
#include "FilesUi.h"
#include "EditUi.h"

void GfxIrqEnter(void) {
    if (gGfxLockDepth++ == 0) {
        gGfxIrqFlags = HalIrqSave();
    }
}


void GfxIrqLeave(void) {
    if (gGfxLockDepth > 0 && --gGfxLockDepth == 0) {
        HalIrqRestore(gGfxIrqFlags);
    }
}


void ComposeBegin(void) {
    gComposeBusy++;
}


void ComposeEnd(void) {
    if (gComposeBusy > 0) {
        gComposeBusy--;
    }
}


/* 主题合成中推迟 Present；拖动等路径仍立即提交 */
void GfxPresent(void) {
    if (gDeferPresent) {
        return;
    }
    HalVideoPresent();
}

void GuiPresentDeferPush(void) {
    gDeferPresent++;
}

void GuiPresentDeferPop(void) {
    if (gDeferPresent > 0) {
        gDeferPresent--;
    }
    if (gDeferPresent == 0) {
        HalVideoPresent();
    }
}


UINT32 TitleBarColor(int Idx) {
    if (Idx == gFocusWin && gWins[Idx].Active) {
        return COLOR_BLUE;
    }
    return COLOR_GRAY;
}


void CloseButtonRect(const GUI_WINDOW *W, UINT32 *Bx, UINT32 *By,
                            UINT32 *Bw, UINT32 *Bh) {
    *Bw = CLOSE_SIZE;
    *Bh = CLOSE_SIZE;
    *Bx = W->X + W->Width - *Bw - CLOSE_MARGIN;
    *By = W->Y + (TITLE_HEIGHT - *Bh) / 2;
}


/* 更高 z（数组下标更大）的窗口是否盖住该像素 */
int PixelOccludedByAbove(int Idx, UINT32 X, UINT32 Y) {
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


void FillRectOccluded(int Idx, UINT32 X, UINT32 Y, UINT32 W, UINT32 H,
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


void DrawHLineOccluded(int Idx, UINT32 X0, UINT32 X1, UINT32 Y,
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


void DrawVLineOccluded(int Idx, UINT32 X, UINT32 Y0, UINT32 Y1,
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


void DrawCloseButton(int Idx, const GUI_WINDOW *W) {
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


void RefreshOtherChrome(int SkipIdx) {
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
void SyncWindowVisualsEx(int ClearDesktop) {
    int i;

    ComposeBegin();
    GfxIrqEnter();
    CursorRestore();
    GfxIrqLeave();
    HalVideoClearClip();
    if (ClearDesktop) {
        DesktopFillRect(0, 0, gScreenW, gScreenH);
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


void SyncWindowVisuals(void) {
    SyncWindowVisualsEx(0);
}


void DrawTitleStringOccluded(int Idx, const GUI_WINDOW *W) {
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


void DrawWindowAtEx(int Idx, int Occlude) {
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


void DrawWindowAt(int Idx) {
    DrawWindowAtEx(Idx, 1);
}


/* 仅重绘标题栏与边框，保留客户区已有文字；不画到上层窗口上 */
void DrawWindowChromeAt(int Idx) {
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


void FillDesktopRectClipped(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
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
                    DesktopFillRect(RunStart, Row, Col - RunStart, 1);
                }
                InRun = 0;
            }
        }
        if (InRun && X + W > RunStart) {
            DesktopFillRect(RunStart, Row, X + W - RunStart, 1);
        }
    }
}


int WindowOccludedByOther(int Idx) {
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


int AllActiveWindowsHaveValidBackup(void) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (gWins[i].Active && !gWinBackupValid[i]) {
            return 0;
        }
    }
    return 1;
}


UINT32 BackupPageCount(UINT32 Ww, UINT32 Wh) {
    UINT64 Bytes = (UINT64)Ww * (UINT64)Wh * sizeof(UINT32);

    return (UINT32)((Bytes + PAGE_SIZE - 1) / PAGE_SIZE);
}


int EnsureWindowBackupBuf(int Idx) {
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


void PreallocWindowBackups(void) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (gWins[i].Active) {
            EnsureWindowBackupBuf(i);
        }
    }
}


/*
 * ForceFull：主题自下而上刚画完本窗、上层尚未覆盖时，必须整窗 ReadRect，
 * 否则 Occluded 路径会跳过重叠区，备份镂空，透视桌面/抬窗花屏。
 */
void BackupWindowAtEx(int Idx, int ForceFull) {
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


void BackupWindowAt(int Idx) {
    BackupWindowAtEx(Idx, 0);
}


/* 与 DrawWindowAt 布局一致；仅作无备份时的回退 */
UINT32 AnalyticWindowPixel(int Idx, UINT32 Px, UINT32 Py) {
    const GUI_WINDOW *W = &gWins[Idx];
    UINT32 Lx;
    UINT32 Ly;

    if (!W->Active) {
        return DesktopBgAt(Px, Py);
    }
    if (Px < W->X || Py < W->Y || Px >= W->X + W->Width || Py >= W->Y + W->Height) {
        return DesktopBgAt(Px, Py);
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


void PaintWindowFromBackup(int Idx) {
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


void ShiftWinBackupsUp(int From, int To) {
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


int RectIntersects(UINT32 Ax, UINT32 Ay, UINT32 Aw, UINT32 Ah,
                          UINT32 Bx, UINT32 By, UINT32 Bw, UINT32 Bh) {
    if (Aw == 0 || Ah == 0 || Bw == 0 || Bh == 0) {
        return 0;
    }
    return Ax < Bx + Bw && Bx < Ax + Aw && Ay < By + Bh && By < Ay + Ah;
}


void ClipRectToScreen(UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H) {
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


UINT32 SampleWindowBackupPixel(int Idx, UINT32 Px, UINT32 Py) {
    const GUI_WINDOW *W = &gWins[Idx];
    UINT32 Bw;
    UINT32 Bh;
    UINT32 Lx;
    UINT32 Ly;

    if (!gWinBackupValid[Idx] || gWinBackup[Idx] == 0 || !W->Active) {
        return DesktopBgAt(Px, Py);
    }
    Bw = gWinBackupW[Idx];
    Bh = gWinBackupH[Idx];
    if (Px < W->X || Py < W->Y) {
        return DesktopBgAt(Px, Py);
    }
    Lx = Px - W->X;
    Ly = Py - W->Y;
    if (Lx >= Bw || Ly >= Bh) {
        return DesktopBgAt(Px, Py);
    }
    return gWinBackup[Idx][Ly * Bw + Lx];
}


int WindowBackupCoversPixel(int Idx, UINT32 Px, UINT32 Py) {
    const GUI_WINDOW *W = &gWins[Idx];

    if (!gWinBackupValid[Idx] || !W->Active) {
        return 0;
    }
    return Px >= W->X && Py >= W->Y &&
           Px < W->X + gWinBackupW[Idx] && Py < W->Y + gWinBackupH[Idx];
}


int PixelCoveredByHigherWindow(int Idx, UINT32 Px, UINT32 Py) {
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


void GuiBackupFocusWindow(void) {
    if (gFocusWin >= 0 && gFocusWin < MAX_WINS && gWins[gFocusWin].Active) {
        BackupWindowAt(gFocusWin);
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
    DesktopFillRect(0, 0, gScreenW, gScreenH);
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


/* PR-G13：菜单开合后清桌面并恢复窗备份（避免全屏 Fill 抹掉刚打开的 Shell） */
void GuiRefreshDesktop(void) {
    SyncWindowVisualsEx(1);
}


void GuiApplyThemeColors(void) {
    int i;
    UINT32 Bg = ThemeShellClientBackground();

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
            gWins[i].Background = ThemeSettingsClientBackground();
        } else if (gWins[i].Active && gWins[i].Kind == GUI_WIN_FILES) {
            gWins[i].Background = ThemeSettingsClientBackground();
        } else if (gWins[i].Active && gWins[i].Kind == GUI_WIN_EDIT) {
            gWins[i].Background = ThemeSettingsClientBackground();
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
    DesktopFillRect(0, 0, gScreenW, gScreenH);
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
            GuiConsoleOpsPaintShellWindow(i);
        } else if (gWins[i].Kind == GUI_WIN_SETTINGS) {
            gFocusWin = i;
            SettingsUiPaintFocused();
        } else if (gWins[i].Kind == GUI_WIN_FILES) {
            gFocusWin = i;
            FilesUiPaintFocused();
        } else if (gWins[i].Kind == GUI_WIN_EDIT) {
            gFocusWin = i;
            EditUiPaintFocused();
        } else if (gWins[i].Kind == GUI_WIN_USER) {
            gFocusWin = i;
            PaintUserClient(i);
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

