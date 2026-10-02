/*
 * FilesUiList.c — 文件列表绘制（PR-UI-layout-apps：UiLayout 三分栏）
 *
 * PaintList 编排；侧栏 / 行表 / 预览见 companion。
 * 核心：FilesUi.c
 */
#include "FilesUiPrivate.h"

void PaintList(void) {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Bg;
    UINT32 LineH;
    UINT32 SideW;
    UINT32 ListW;
    UINT32 DetailW;
    UINT32 Cx;
    UINT32 Cw;

    if (!GuiFocusClient(&X, &Y, &W, &H, &Bg)) {
        return;
    }
    GuiFrameBufferBegin();
    HalVideoFillRect(X, Y, W, H, Bg);
    HalVideoSetClipRegion(X, Y, W, H, Bg);

    LineH = UiLayoutRowH();
    UiLayoutTriple(W, &SideW, &ListW, &DetailW);
    gSideW = SideW;
    gPrevW = DetailW;
    gContentX = X + SideW;
    gContentW = W - SideW;
    Cx = gContentX;
    Cw = gContentW;

    PaintListDrawSide(X, Y, H, LineH, SideW);
    PaintListDrawHeader(Cx, Y, LineH, Cw);
    PaintListDrawRows(Cx, ListW, Y, H, LineH);
    PaintListDrawPreview(Cx, Y, H, LineH, Cw);

    HalVideoClearClip();
    GuiBackupFocusWindow();
    GuiFrameBufferEnd();
}
