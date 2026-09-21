/*
 * GuiDraw.c — 窗绘制入口（核心）
 * 辅助：GuiDrawChrome.c / GuiDrawShadow.c
 *
 * 从 GuiDraw.c 单体迁出；只搬家、不改逻辑。
 */
#include "GuiPrivate.h"
#include "UI.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Theme.h"
#include "Font.h"
#include "Desktop.h"

UINT32 TitleBarColor(int Idx) {
    if (!gWindows[Idx].Active) {
        return ThemeWindowTitleIdle();
    }
    if (Idx == gFocusWin) {
        return ThemeWindowTitleFocus();
    }
    if (Idx == gHoverWin) {
        return ThemeWindowTitleHover();
    }
    return ThemeWindowTitleIdle();
}

/* 通道逐行插值：Row=0 → Top，Row=TitleH-1 → Bottom（PR-GUI-l2-gradient） */
UINT32 TitleBarColorAtRow(int Idx, UINT32 Row, UINT32 TitleH) {
    UINT32 Top;
    UINT32 Bot;
    UINT8 T;

    Top = TitleBarColor(Idx);
    if (!ThemeIsGradientEnabled() || TitleH <= 1u) {
        return Top;
    }
    if (Row >= TitleH) {
        Row = TitleH - 1u;
    }
    Bot = ThemeWindowTitleGradientBottom(Top);
    T = (UINT8)((Row * 255u) / (TitleH - 1u));
    return UiBlendRgb(Top, Bot, T);
}

UINT32 WindowBorderColor(int Idx) {
    if (!gWindows[Idx].Active) {
        return ThemeWindowBorderIdle();
    }
    if (Idx == gFocusWin) {
        return ThemeWindowBorderFocus();
    }
    if (Idx == gHoverWin) {
        return ThemeWindowBorderHover();
    }
    return ThemeWindowBorderIdle();
}

/* 最顶层（数组下标最大）命中窗；无则 -1 */
int TopWindowAt(UINT32 X, UINT32 Y) {
    int i;
    int Top = -1;

    for (i = 0; i < MAX_WINS; i++) {
        if (PointInWindow(&gWindows[i], X, Y)) {
            Top = i;
        }
    }
    return Top;
}

void GuiHoverUpdate(UINT32 X, UINT32 Y) {
    int Next;
    int Prev;

    /*
     * 开始菜单叠在所有窗之上，但不是 GUI_WINDOW。
     * 若仍按命中窗重画 chrome，会从菜单「镂」出标题栏/边框方块烙印。
     */
    if (DesktopStartMenuIsOpen()) {
        gHoverWin = -1;
        return;
    }

    Next = TopWindowAt(X, Y);
    Prev = gHoverWin;

    if (Next == Prev) {
        return;
    }
    gHoverWin = Next;
    /* 焦点窗标题已是 Focus 色，悬停进出不必重画它的 chrome */
    if ((Prev >= 0 && Prev != gFocusWin && Prev < MAX_WINS && gWindows[Prev].Active) ||
        (Next >= 0 && Next != gFocusWin && Next < MAX_WINS && gWindows[Next].Active)) {
        GuiFrameBufferBegin();
        if (Prev >= 0 && Prev < MAX_WINS && Prev != gFocusWin && gWindows[Prev].Active) {
            DrawWindowChromeAt(Prev);
        }
        if (Next >= 0 && Next < MAX_WINS && Next != gFocusWin && gWindows[Next].Active) {
            DrawWindowChromeAt(Next);
        }
        GuiFrameBufferEnd();
    }
}

/*
 * ClearDesktop：先铺桌面再贴窗。拖动结束后必须清底，否则旧 footprint 外的
 * 标题栏/关闭钮残影不会被「只贴窗矩形」的路径擦掉。
 * 阴影第二遍统一画：避免「先画 A 影到 B 上，再 WriteRect B」次序错乱，
 * 也避免 PaintWindowFromBackup 把影写进下层后再被 Backup 吸入。
 */
void SyncWindowVisualsEx(int ClearDesktop) {
    int i;

    ComposeBeginEraseCursor();
    HalVideoClearClip();
    if (ClearDesktop) {
        DesktopFillRect(0, 0, gScreenWidth, gScreenHeight);
        DesktopDraw();
    }
    for (i = 0; i < MAX_WINS; i++) {
        if (!gWindows[i].Active) {
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
            /* 本体；阴影留第二遍 */
            DrawWindowAtEx(i, WindowOccludedByOther(i) ? 1 : 0);
        }
    }
    for (i = 0; i < MAX_WINS; i++) {
        if (gWindows[i].Active) {
            DrawWindowShadowAt(i);
        }
    }
    DesktopDrawStartMenu();
    DesktopDrawNetTrayPopup();
    GfxIrqEnter();
    CursorPaint();
    GfxIrqLeave();
    HalVideoPresentFlush();
    ComposeEnd();
}


void SyncWindowVisuals(void) {
    SyncWindowVisualsEx(0);
}

void DrawWindowAtEx(int Idx, int Occlude) {
    const GUI_WINDOW *W = &gWindows[Idx];
    UINT32 Border;
    UINT32 Th;
    UINT32 Row;

    if (!W->Active) {
        return;
    }
    Border = WindowBorderColor(Idx);
    Th = TITLE_HEIGHT;
    if (Th > W->Height) {
        Th = W->Height;
    }
    /* 标题在客户区外；若仍开着 Shell/Settings clip，DrawString 会被裁掉 */
    HalVideoClearClip();
    /* 阴影由调用方或 Sync 第二遍画，避免与下层 WriteRect 次序打架 */
    if (Occlude) {
        for (Row = 0; Row < Th; Row++) {
            FillRectOccluded(Idx, W->X, W->Y + Row, W->Width, 1,
                             TitleBarColorAtRow(Idx, Row, Th));
        }
        DrawHLineOccluded(Idx, W->X, W->X + W->Width - 1, W->Y, Border);
        DrawHLineOccluded(Idx, W->X, W->X + W->Width - 1, W->Y + W->Height - 1,
                          Border);
        DrawVLineOccluded(Idx, W->X, W->Y, W->Y + W->Height - 1, Border);
        DrawVLineOccluded(Idx, W->X + W->Width - 1, W->Y, W->Y + W->Height - 1,
                          Border);
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
    for (Row = 0; Row < Th; Row++) {
        HalVideoFillRect(W->X, W->Y + Row, W->Width, 1,
                         TitleBarColorAtRow(Idx, Row, Th));
    }
    HalVideoFillRect(W->X, W->Y, W->Width, 1, Border);
    HalVideoFillRect(W->X, W->Y + W->Height - 1, W->Width, 1, Border);
    HalVideoFillRect(W->X, W->Y, 1, W->Height, Border);
    HalVideoFillRect(W->X + W->Width - 1, W->Y, 1, W->Height, Border);
    if (W->Width > 2 && W->Height > TITLE_HEIGHT + 1) {
        HalVideoFillRect(W->X + 1, W->Y + TITLE_HEIGHT, W->Width - 2,
                         W->Height - TITLE_HEIGHT - 1, W->Background);
    }
    if (W->Title != 0 && W->Title[0] != 0) {
        HalVideoDrawStringAt(W->X + 8, W->Y + 4, W->Title, ThemeWindowTitleText());
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
        UINT32 Edge = ThemeWindowBorderFocus();

        CloseButtonRect(W, &Bx, &By, &Bw, &Bh);
        HalVideoFillRect(Bx, By, Bw, Bh, ThemeCloseButton());
        if (Bw >= 2 && Bh >= 2) {
            HalVideoFillRect(Bx, By, Bw, 1, Edge);
            HalVideoFillRect(Bx, By + Bh - 1, Bw, 1, Edge);
            HalVideoFillRect(Bx, By, 1, Bh, Edge);
            HalVideoFillRect(Bx + Bw - 1, By, 1, Bh, Edge);
        }
        Pad = 7;
        if (Bw > Pad * 2 + 2 && Bh > Pad * 2 + 2) {
            Span = Bw - 1 - Pad * 2;
            for (I = 0; I <= Span; I++) {
                HalVideoDrawPixelRaw(Bx + Pad + I, By + Pad + I, ThemeWindowTitleText());
                HalVideoDrawPixelRaw(Bx + Bw - 1 - Pad - I, By + Pad + I,
                                     ThemeWindowTitleText());
            }
        }
    }
}


void DrawWindowAt(int Idx) {
    DrawWindowAtEx(Idx, 1);
    DrawWindowShadowAt(Idx);
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
        if (!gWindows[j].Active) {
            continue;
        }
        if (RectIntersects(gWindows[Idx].X, gWindows[Idx].Y, gWindows[Idx].Width, gWindows[Idx].Height,
                           gWindows[j].X, gWindows[j].Y, gWindows[j].Width, gWindows[j].Height)) {
            return 1;
        }
    }
    return 0;
}
