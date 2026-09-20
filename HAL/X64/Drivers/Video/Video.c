/*
 * Video.c — GOP 帧缓冲驱动（PR-G9：可选 backbuffer + 脏矩形 Present）
 *
 * 绘制写入后缓冲（若已启用）；VideoPresent 将脏区一次 blit 到 scanout。
 * PR-G-present：脏区按行 memcpy（非整段逐像素）。
 * 字形经 Font_*（Common/Fonts/），不直接绑定某一份点阵表。
 */
#include "VideoPrivate.h"

/* 全局定义集中在宿主；其它 TU 经 VideoPrivate.h extern */
SCREEN_INFO gScreen = {0};
UINT32 gBackground = 0x00000000;
int gClipOn;
UINT32 gClipX;
UINT32 gClipY;
UINT32 gClipW;
UINT32 gClipH;
UINT32 gClipBg;

/* scanout（GOP）与后缓冲 */
UINT32 *gFront;
UINT32  gFrontPitch;
UINT32 *gBack;
UINT32  gBackPitch;
UINT32  gBackPages;
int     gBackOn;
/* 真机 boot mark：直写 scanout，避开后缓冲 Present 假死 */
int     gForceFront;
/* UI 缩放：逻辑坐标画后缓冲，Present 最近邻贴到物理 GOP（50/100/150/200） */
UINT32  gPhysW;
UINT32  gPhysH;
UINT32  gUiScale = 100;

/* 内容脏矩形 [gDx0,gDx1) x [gDy0,gDy1)；光标 XOR 单独跟踪，避免 AABB 并成近全屏 */
int     gDirty;
UINT32  gDx0;
UINT32  gDy0;
UINT32  gDx1;
UINT32  gDy1;
int     gCurDirty;
UINT32  gCx0;
UINT32  gCy0;
UINT32  gCx1;
UINT32  gCy1;
/* 光标叠层绘制：DirtyUnion 改记光标矩形 */
int     gCursorOverlay;

void VideoDrawBeginFront(void) {
    gForceFront = 1;
}

void VideoDrawEndFront(void) {
    gForceFront = 0;
}

void VideoReleaseBackbuffer(void) {
    if (gBack && gBackPages) {
        PhysicalMemoryFreePages(gBack, gBackPages);
    }
    gBack = 0;
    gBackPitch = 0;
    gBackPages = 0;
    gBackOn = 0;
    gDirty = 0;
    gCurDirty = 0;
}

UINT64 VideoFrameBufferBase(void) {
    return gScreen.FrameBufferBase;
}

UINT64 VideoFrameBufferSize(void) {
    return gScreen.FrameBufferSize;
}

/*
 * 启用与屏同尺寸的后缓冲（紧密 pitch=Width）。Buf 由调用方 PMM 分配。
 * Pages 仅记录；失败/空指针则保持直写 GOP。
 */
void VideoSetBackbuffer(UINT32 *Buf, UINT32 Pages) {
    if (!Buf || gScreen.Width == 0 || gScreen.Height == 0 || !gFront) {
        gBack = 0;
        gBackPages = 0;
        gBackOn = 0;
        return;
    }
    gBack = Buf;
    gBackPitch = gScreen.Width;
    gBackPages = Pages;
    gBackOn = 1;
    /*
     * 不从 GOP 全屏拷：后缓冲内容以后续绘制为准。
     * PR-K-log-cont：InitVideo 不再 ClearScreen+Present 抹掉 boot 上滚。
     */
    gDirty = 0;
    gCurDirty = 0;
}

int VideoBackbufferEnabled(void) {
    return gBackOn;
}

UINT32 VideoBackbufferPages(void) {
    return gBackPages;
}

void VideoGetSize(UINT32 *Width, UINT32 *Height) {
    if (Width) {
        *Width = gScreen.Width;
    }
    if (Height) {
        *Height = gScreen.Height;
    }
}

void VideoSetClipRegion(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Background) {
    gClipOn = 1;
    gClipX = X;
    gClipY = Y;
    gClipW = Width;
    gClipH = Height;
    gClipBg = Background;
    gBackground = Background;
}

void VideoSetClipOrigin(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Background) {
    VideoSetClipRegion(X, Y, Width, Height, Background);
    gScreen.CursorX = X;
    gScreen.CursorY = Y;
}

void VideoGetTextCursor(UINT32 *X, UINT32 *Y) {
    if (X) {
        *X = gScreen.CursorX;
    }
    if (Y) {
        *Y = gScreen.CursorY;
    }
}

void VideoSetTextCursor(UINT32 X, UINT32 Y) {
    gScreen.CursorX = X;
    gScreen.CursorY = Y;
}

void VideoClearClip(void) {
    gClipOn = 0;
}
