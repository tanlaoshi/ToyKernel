/*
 * GuiUserBlit.c — 客户区文字、按钮绘制与像素 blit
 * 核心：GuiUser.c
 */
#include "GuiPrivate.h"
#include "UI.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Debug.h"
#include "Theme.h"
#include "Font.h"

void PaintUserClient(int Idx) {
    GUI_WINDOW *W;
    UINT32 Cx;
    UINT32 Cy;
    UINT32 Cw;
    UINT32 Ch;
    int Bi;
    UINT32 Bx;
    UINT32 By;
    UINT32 Bw;
    UINT32 Bh;
    UINT32 Gap = 8;
    int Count;
    int Slot;
    UINT32 ClientX;
    UINT32 ClientY;
    UINT32 ClientW;
    UINT32 ClientH;

    if (Idx < 0 || Idx >= MAX_WINS) {
        return;
    }
    W = &gWindows[Idx];
    if (!W->Active || W->Kind != GUI_WIN_USER) {
        return;
    }
    if (W->Width <= 2 + GUI_CLIENT_PAD * 2 ||
        W->Height <= TITLE_HEIGHT + 1 + GUI_CLIENT_PAD * 2) {
        return;
    }
    ClientX = W->X + 1;
    ClientY = W->Y + TITLE_HEIGHT;
    ClientW = W->Width - 2;
    ClientH = W->Height - TITLE_HEIGHT - 1;
    Cx = W->X + 1 + GUI_CLIENT_PAD;
    Cy = W->Y + TITLE_HEIGHT + GUI_CLIENT_PAD;
    Cw = W->Width - 2 - GUI_CLIENT_PAD * 2;
    Ch = W->Height - TITLE_HEIGHT - 1 - GUI_CLIENT_PAD * 2;
    HalVideoClearClip();
    /*
     * 顶层用户窗必须用不透明 Fill（勿 FillRectOccluded）：
     * 否则重叠区易留下下层 Shell 文字 →「透视」。
     */
    if (WindowOccludedByOther(Idx)) {
        FillRectOccluded(Idx, ClientX, ClientY, ClientW, ClientH, W->Background);
    } else {
        HalVideoFillRect(ClientX, ClientY, ClientW, ClientH, W->Background);
    }
    if (W->ClientText[0] != 0) {
        HalVideoSetClipOrigin(Cx, Cy, Cw, Ch, W->Background);
        HalVideoDrawStringAt(Cx, Cy, W->ClientText, ThemeText());
        HalVideoClearClip();
    }
    Count = 0;
    for (Bi = 0; Bi < 4; Bi++) {
        if (W->UserButtonUsed[Bi]) {
            Count++;
        }
    }
    if (Count == 0 || Ch < 48) {
        return;
    }
    Bh = 36;
    Bw = (Cw - Gap * (Count + 1)) / (UINT32)Count;
    if (Bw < 64) {
        Bw = 64;
    }
    if (Bw > 160) {
        Bw = 160;
    }
    By = Cy + Ch - Bh - 4;
    Bx = Cx + Gap;
    HalVideoSetClipOrigin(Cx, Cy, Cw, Ch, W->Background);
    Slot = 0;
    for (Bi = 0; Bi < 4; Bi++) {
        if (!W->UserButtonUsed[Bi]) {
            continue;
        }
        UiDrawButton(Bx + Slot * (Bw + Gap), By, Bw, Bh,
                     W->UserButtonLabel[Bi], ThemeText(), ThemeControlFace());
        Slot++;
    }
    HalVideoClearClip();
}

int GuiDamageUser(int Wid, const char *Text) {
    if (Wid < 0 || Wid >= MAX_WINS) {
        return -1;
    }
    /* Raise 前用 Wid 写文案；槽位移动后 Repaint 用 gFocusWin */
    if (!gWindows[Wid].Active || gWindows[Wid].Kind != GUI_WIN_USER) {
        return -1;
    }
    CopyTitleBuf(gWindows[Wid].ClientText, sizeof(gWindows[Wid].ClientText), Text);
    RepaintUserWindow(Wid);
    return 0;
}


/*
 * PR-G-desk-3：客户区相对坐标像素 blit。
 * 坐标原点 = 客户区左上（含 GUI_CLIENT_PAD）；有按钮时避开底栏。
 */
int GuiDamageRectUser(int Wid, UINT32 X, UINT32 Y, UINT32 W, UINT32 H,
                      const UINT32 *Pixels) {
    GUI_WINDOW *Win;
    UINT32 Cx;
    UINT32 Cy;
    UINT32 Cw;
    UINT32 Ch;
    UINT32 MaxH;
    UINT32 SrcX0;
    UINT32 SrcY0;
    UINT32 SrcX1;
    UINT32 SrcY1;
    UINT32 ClipW;
    UINT32 ClipH;
    UINT32 ScreenX;
    UINT32 ScreenY;
    UINT32 OffX;
    UINT32 OffY;
    UINT32 Row;
    int Idx;
    int Bi;
    int Count;

    if (Wid < 0 || Wid >= MAX_WINS || !Pixels || W == 0 || H == 0) {
        return -1;
    }
    if (!gWindows[Wid].Active || gWindows[Wid].Kind != GUI_WIN_USER) {
        return -1;
    }
    Idx = UserWindowIndexAfterRaise(Wid);
    if (Idx < 0) {
        return -1;
    }
    Win = &gWindows[Idx];
    if (Win->Width <= 2 + GUI_CLIENT_PAD * 2 ||
        Win->Height <= TITLE_HEIGHT + 1 + GUI_CLIENT_PAD * 2) {
        return -1;
    }
    Cx = Win->X + 1 + GUI_CLIENT_PAD;
    Cy = Win->Y + TITLE_HEIGHT + GUI_CLIENT_PAD;
    Cw = Win->Width - 2 - GUI_CLIENT_PAD * 2;
    Ch = Win->Height - TITLE_HEIGHT - 1 - GUI_CLIENT_PAD * 2;
    MaxH = Ch;
    Count = 0;
    for (Bi = 0; Bi < 4; Bi++) {
        if (Win->UserButtonUsed[Bi]) {
            Count++;
        }
    }
    if (Count > 0 && Ch >= 48) {
        /* 与 PaintUserClient / UserButtonHit 底栏一致：Bh=36 + 下边距 4 */
        if (MaxH > 40) {
            MaxH -= 40;
        } else {
            MaxH = 0;
        }
    }
    if (X >= Cw || Y >= MaxH) {
        return -1;
    }
    SrcX0 = X;
    SrcY0 = Y;
    SrcX1 = X + W;
    SrcY1 = Y + H;
    if (SrcX1 > Cw) {
        SrcX1 = Cw;
    }
    if (SrcY1 > MaxH) {
        SrcY1 = MaxH;
    }
    ClipW = SrcX1 - SrcX0;
    ClipH = SrcY1 - SrcY0;
    if (ClipW == 0 || ClipH == 0) {
        return -1;
    }
    ScreenX = Cx + SrcX0;
    ScreenY = Cy + SrcY0;
    OffX = SrcX0 - X;
    OffY = SrcY0 - Y;

    GuiFrameBufferBegin();
    HalVideoClearClip();
    if (OffX == 0 && OffY == 0 && ClipW == W && ClipH == H) {
        HalVideoWriteRect(ScreenX, ScreenY, ClipW, ClipH, Pixels);
    } else {
        for (Row = 0; Row < ClipH; Row++) {
            HalVideoWriteRect(ScreenX, ScreenY + Row, ClipW, 1,
                              &Pixels[(OffY + Row) * W + OffX]);
        }
    }
    GuiBackupSyncRect(ScreenX, ScreenY, ClipW, ClipH);
    GuiFrameBufferEnd();
    return 0;
}
