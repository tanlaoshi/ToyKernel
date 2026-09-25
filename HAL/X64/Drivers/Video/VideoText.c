/*
 * VideoText.c — 滚动与光标文本（PR-S-video-1）
 */
#include "VideoPrivate.h"

void VideoClearScreen(UINT32 Color) {
    if (!gFront && !gBack) {
        return;
    }
    VideoFillRect(0, 0, gScreen.Width, gScreen.Height, Color);
    gScreen.CursorX = 0;
    gScreen.CursorY = 0;
    gBackground = Color;
}

static void ScrollClip(void) {
    UINT32 *Fb = DrawBase();
    UINT32 Pitch = DrawPitch();
    UINT32 LineHeight = FontAdvanceY();
    UINT32 Y;
    UINT32 X;
    UINT32 XEnd;
    UINT32 YEnd;

    if (!Fb || Pitch == 0 || gClipW == 0 || gClipH <= LineHeight) {
        return;
    }
    XEnd = gClipX + gClipW;
    YEnd = gClipY + gClipH;
    for (Y = gClipY; Y + LineHeight < YEnd; Y++) {
        for (X = gClipX; X < XEnd; X++) {
            Fb[Y * Pitch + X] = Fb[(Y + LineHeight) * Pitch + X];
        }
    }
    for (Y = YEnd - LineHeight; Y < YEnd; Y++) {
        for (X = gClipX; X < XEnd; X++) {
            Fb[Y * Pitch + X] = gClipBg;
        }
    }
    DirtyUnion(gClipX, gClipY, gClipW, gClipH);
}

/* 内容下移一行（顶部空出），与 ScrollClip 相反 */
static void ScrollClipDown(void) {
    UINT32 *Fb = DrawBase();
    UINT32 Pitch = DrawPitch();
    UINT32 LineHeight = FontAdvanceY();
    UINT32 Y;
    UINT32 X;
    UINT32 XEnd;
    UINT32 YEnd;

    if (!Fb || Pitch == 0 || !gClipOn || gClipW == 0 || gClipH <= LineHeight) {
        return;
    }
    XEnd = gClipX + gClipW;
    YEnd = gClipY + gClipH;
    for (Y = YEnd; Y > gClipY + LineHeight; ) {
        Y--;
        for (X = gClipX; X < XEnd; X++) {
            Fb[Y * Pitch + X] = Fb[(Y - LineHeight) * Pitch + X];
        }
    }
    for (Y = gClipY; Y < gClipY + LineHeight; Y++) {
        for (X = gClipX; X < XEnd; X++) {
            Fb[Y * Pitch + X] = gClipBg;
        }
    }
    DirtyUnion(gClipX, gClipY, gClipW, gClipH);
}

void VideoScrollClipLines(int Delta) {
    int n;
    int i;

    if (!gClipOn || Delta == 0) {
        return;
    }
    n = Delta > 0 ? Delta : -Delta;
    if (n > 32) {
        n = 32;
    }
    for (i = 0; i < n; i++) {
        if (Delta > 0) {
            ScrollClipDown();
        } else {
            ScrollClip();
        }
    }
}

static void ScrollScreen(void) {
    UINT32 *Fb = DrawBase();
    UINT32 Pitch = DrawPitch();
    UINT32 LineHeight;
    UINT32 Y;
    UINT32 X;
    UINT32 W;

    if (!Fb || Pitch == 0) {
        return;
    }
    if (gClipOn) {
        ScrollClip();
        return;
    }

    LineHeight = FontAdvanceY();
    W = gScreen.Width;
    for (Y = 0; Y < gScreen.Height - LineHeight; Y++) {
        for (X = 0; X < W; X++) {
            Fb[Y * Pitch + X] = Fb[(Y + LineHeight) * Pitch + X];
        }
    }
    for (Y = gScreen.Height - LineHeight; Y < gScreen.Height; Y++) {
        for (X = 0; X < W; X++) {
            Fb[Y * Pitch + X] = gBackground;
        }
    }
    DirtyUnion(0, 0, gScreen.Width, gScreen.Height);
}

void VideoNewLine(void) {
    UINT32 LineHeight = FontAdvanceY();

    if ((!gFront && !gBack) || gScreen.Height == 0 || gScreen.Width == 0) {
        return;
    }

    if (gClipOn) {
        if (gClipH <= LineHeight) {
            gScreen.CursorX = gClipX;
            return;
        }
        gScreen.CursorX = gClipX;
        gScreen.CursorY += LineHeight;
        while (gScreen.CursorY + FontCellH() > gClipY + gClipH) {
            ScrollClip();
            if (gScreen.CursorY < LineHeight) {
                gScreen.CursorY = gClipY;
                break;
            }
            gScreen.CursorY -= LineHeight;
        }
        return;
    }

    gScreen.CursorX = 0;
    gScreen.CursorY += LineHeight;

    while (gScreen.CursorY + FontCellH() > gScreen.Height) {
        ScrollScreen();
        if (gScreen.CursorY < LineHeight) {
            gScreen.CursorY = 0;
            break;
        }
        gScreen.CursorY -= LineHeight;
    }
}

void VideoDrawChar(char c, UINT32 Color) {
    if (c == '\n') {
        VideoNewLine();
        return;
    }
    if ((UINT8)c < 32 || (UINT8)c > 126) {
        return;
    }
    VideoDrawCodepoint(c, Color);
}

void VideoDrawCodepoint(UINT32 Cp, UINT32 Color) {
    UINT32 MaxX;
    UINT32 MaxY;
    UINT32 Adv;

    if (Cp == 0) {
        return;
    }
    if (Cp == '\n') {
        VideoNewLine();
        return;
    }
    if ((!gFront && !gBack) || gScreen.Width == 0 || gScreen.Height == 0) {
        return;
    }
    if (Cp < 128 && (Cp < 32 || Cp > 126)) {
        return;
    }
    if (Cp >= 128 && !FontGlyphCp(Cp, (UINT32 *)0, (UINT32 *)0)) {
        return;
    }

    if (gClipOn) {
        MaxX = gClipX + gClipW;
        MaxY = gClipY + gClipH;
        if (gScreen.CursorX < gClipX) {
            gScreen.CursorX = gClipX;
        }
        if (gScreen.CursorY < gClipY) {
            gScreen.CursorY = gClipY;
        }
    } else {
        MaxX = gScreen.Width;
        MaxY = gScreen.Height;
    }

    Adv = CodepointAdvance(Cp);
    if (gScreen.CursorX + Adv > MaxX) {
        VideoNewLine();
    }

    if (gScreen.CursorY + FontCellH() > MaxY) {
        VideoNewLine();
    }

    VideoDrawCodepointAt(gScreen.CursorX, gScreen.CursorY, Cp, Color);
    gScreen.CursorX += Adv;
}

void VideoEraseLastChar(void) {
    UINT32 Step = FontAdvanceX();
    UINT32 MinX = gClipOn ? gClipX : 0;

    if (gScreen.CursorX < MinX + Step) {
        return;
    }
    gScreen.CursorX -= Step;
    for (UINT32 Row = 0; Row < FontCellH(); Row++) {
        for (UINT32 Col = 0; Col < Step; Col++) {
            VideoDrawPixel(gScreen.CursorX + Col, gScreen.CursorY + Row, gBackground);
        }
    }
}

void VideoDrawString(const char *Text, UINT32 Color) {
    while (Text && *Text) {
        UINT32 Cp;
        UINTN N;

        if (*Text == '\n') {
            VideoNewLine();
            Text++;
            continue;
        }
        N = Utf8Decode(Text, &Cp);
        if (N == 0) {
            Text++;
            continue;
        }
        VideoDrawCodepoint(Cp, Color);
        Text += N;
    }
}
