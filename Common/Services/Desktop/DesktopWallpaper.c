/*
 * DesktopWallpaper.c — 壁纸缓存 / DesktopBgAt / DesktopFillRect（PR-S-desktop-split-1）
 *
 * 从 Desktop.c 迁出；只搬家、不改逻辑。
 */
#include "DesktopPrivate.h"

void FreeWallScreen(void) {
    if (gWallScreen && gWallScreenPages) {
        PhysicalMemoryFreePages(gWallScreen, gWallScreenPages);
    }
    gWallScreen = 0;
    gWallScreenW = 0;
    gWallScreenH = 0;
    gWallScreenPages = 0;
}

void BuildWallScreen(void) {
    UINT32 Sw;
    UINT32 Sh;
    UINT64 Bytes;
    UINT32 Pages;
    UINT32 Y;
    UINT32 X;
    UINT32 WallW;
    UINT32 WallH;
    UINT32 *WallPix;
    UINT32 *Dst;

    HalVideoGetSize(&Sw, &Sh);
    if (Sw == 0 || Sh == 0) {
        return;
    }
    if (gWallScreen && gWallScreenW == Sw && gWallScreenH == Sh) {
        return;
    }
    FreeWallScreen();
    if (!gWallReady || !gWall.Pixels || gWall.Width == 0 || gWall.Height == 0) {
        return;
    }
    /* 快照尺寸与指针，避免缩放循环中被重入释放 */
    WallPix = gWall.Pixels;
    WallW = gWall.Width;
    WallH = gWall.Height;
    Bytes = (UINT64)Sw * (UINT64)Sh * sizeof(UINT32);
    Pages = (UINT32)((Bytes + 4095ull) / 4096ull);
    if (Pages == 0) {
        return;
    }
    Dst = (UINT32 *)PhysicalMemoryAllocatePages(Pages);
    if (!Dst) {
        return;
    }
    for (Y = 0; Y < Sh; Y++) {
        UINT32 Sy = (Y * WallH) / Sh;
        if (Sy >= WallH) {
            Sy = WallH - 1;
        }
        for (X = 0; X < Sw; X++) {
            UINT32 Sx = (X * WallW) / Sw;
            if (Sx >= WallW) {
                Sx = WallW - 1;
            }
            Dst[Y * Sw + X] = WallPix[Sy * WallW + Sx];
        }
    }
    gWallScreen = Dst;
    gWallScreenPages = Pages;
    gWallScreenW = Sw;
    gWallScreenH = Sh;
}

void LoadWallpaper(void) {
    FreeWallScreen();
    gWallReady = LoadBmpPath("Assets/Images/WALL.BMP", &gWall, WALL_FILE_MAX,
                             "desktop: wallpaper");
    if (gWallReady) {
        BuildWallScreen();
    }
}

UINT32 DesktopBgAt(UINT32 X, UINT32 Y) {
    UINT32 Sw;
    UINT32 Sh;

    BuildWallScreen();
    if (gWallScreen && gWallScreenW && gWallScreenH) {
        if (X >= gWallScreenW) {
            X = gWallScreenW - 1;
        }
        if (Y >= gWallScreenH) {
            Y = gWallScreenH - 1;
        }
        return gWallScreen[Y * gWallScreenW + X];
    }
    HalVideoGetSize(&Sw, &Sh);
    (void)Sw;
    (void)Sh;
    return ThemeDesktopBackground();
}

void DesktopFillRect(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    UINT32 Row;
    UINT32 Sw;
    UINT32 Sh;
    UINT32 CopyW;

    if (W == 0 || H == 0) {
        return;
    }
    BuildWallScreen();
    if (!gWallScreen) {
        UiFillRectangle(X, Y, W, H, ThemeDesktopBackground());
        return;
    }
    Sw = gWallScreenW;
    Sh = gWallScreenH;
    if (X >= Sw || Y >= Sh) {
        return;
    }
    if (X + W > Sw) {
        W = Sw - X;
    }
    if (Y + H > Sh) {
        H = Sh - Y;
    }
    CopyW = W;
    for (Row = 0; Row < H; Row++) {
        HalVideoWriteRect(X, Y + Row, CopyW, 1,
                          &gWallScreen[(Y + Row) * Sw + X]);
    }
}

/* 只填未被窗占用的像素（图标拖动擦旧脚印，勿盖标题栏/客户区） */
void DesktopFillRectFree(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    UINT32 Row;
    UINT32 Col;
    UINT32 RunStart;
    int InRun;

    if (W == 0 || H == 0) {
        return;
    }
    for (Row = Y; Row < Y + H; Row++) {
        InRun = 0;
        RunStart = 0;
        for (Col = X; Col < X + W; Col++) {
            int Free = !PointOccupied(Col, Row);
            if (Free && !InRun) {
                RunStart = Col;
                InRun = 1;
            } else if (!Free && InRun) {
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

