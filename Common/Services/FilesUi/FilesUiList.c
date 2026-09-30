/*
 * FilesUiList.c — 文件列表绘制（PR-F-filesui-1）
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
    UINT32 Cx;
    UINT32 Cw;
    UINT32 ListW;

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

    /* PR-U1/U2：左栏固定宽 + 书签；窄窗退回单栏 */
    SideW = 0;
    gSideW = 0;
    if (W > FILES_SIDE_W + 160u) {
        SideW = FILES_SIDE_W;
    }
    gContentX = X + SideW;
    gContentW = W - SideW;
    Cx = gContentX;
    Cw = gContentW;

    PaintListDrawSide(X, Y, H, LineH, SideW);
    PaintListDrawHeader(Cx, Y, LineH, Cw);

    ListW = Cw - gPrevW;
    PaintListDrawRows(Cx, ListW, Y, H, LineH);
    PaintListDrawPreview(Cx, Y, H, LineH, Cw);

    HalVideoClearClip();
    GuiBackupFocusWindow();
    GuiFrameBufferEnd();
}
