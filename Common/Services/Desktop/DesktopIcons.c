/*
 * DesktopIcons.c — 图标 BMP / 布局 / 拖放移动（PR-S-desktop-split-3）
 *
 * 从 Desktop.c 迁出；只搬家、不改逻辑。
 */
#include "DesktopPrivate.h"

int PathHasVolPrefix(const char *Path) {
    int i;

    if (!Path) {
        return 0;
    }
    for (i = 0; Path[i] && i < 16; i++) {
        if (Path[i] == ':') {
            return 1;
        }
        if (Path[i] == '/' || Path[i] == '\\') {
            return 0;
        }
    }
    return 0;
}

/* 读 TOYOS（或显式路径）上 BI_RGB BMP；成功返回 1。失败不内嵌，由绘制侧纯色回退。 */
int LoadBmpPath(const char *Path, BMP_IMAGE *Out, UINT32 FileMax,
                       const char *Tag) {
    UINT8 *Buf;
    UINT32 Pages;
    UINTN Size;
    int Err;
    char ToyPath[192];
    const char *TryPath;
    int Pass;

    (void)Tag;
    if (!Path || !Out || FileMax < 54) {
        return 0;
    }
    BmpFree(Out);
    Pages = (FileMax + 4095u) / 4096u;
    Buf = (UINT8 *)PhysicalMemoryAllocatePages(Pages);
    if (!Buf) {
        DebugWrite(Tag);
        DebugWrite(": alloc failed\n");
        return 0;
    }

    for (Pass = 0; Pass < 2; Pass++) {
        if (Pass == 0) {
            TryPath = Path;
        } else if (PathHasVolPrefix(Path)) {
            break;
        } else {
            /* 默认卷可能是 ESP；再显式试 TOYOS: */
            ToyPath[0] = 'T';
            ToyPath[1] = 'O';
            ToyPath[2] = 'Y';
            ToyPath[3] = 'O';
            ToyPath[4] = 'S';
            ToyPath[5] = ':';
            {
                int i;
                for (i = 0; Path[i] && i < (int)sizeof(ToyPath) - 7; i++) {
                    ToyPath[6 + i] = Path[i];
                }
                ToyPath[6 + i] = 0;
            }
            TryPath = ToyPath;
        }

        Size = 0;
        Err = FileSystemReadFile(TryPath, Buf, FileMax, &Size);
        if (Err != FAT_OK || Size < 54) {
            continue;
        }
        if (BmpDecode(Buf, Size, Out) != 0) {
            DebugWrite(Tag);
            DebugWrite(": decode failed ");
            DebugWrite(TryPath);
            DebugWrite("\n");
            continue;
        }
        PhysicalMemoryFreePages(Buf, Pages);
        DebugWrite(Tag);
        DebugWrite(": loaded ");
        DebugWrite(TryPath);
        DebugWrite("\n");
        return 1;
    }

    PhysicalMemoryFreePages(Buf, Pages);
    DebugWrite(Tag);
    DebugWrite(": missing ");
    DebugWrite(Path);
    DebugWrite(" (solid fallback)\n");
    return 0;
}

void LoadDesktopIcons(void) {
    int i;
    static const char *const SysTags[DESKTOP_SYS_ICON_COUNT] = {
        "desktop: shell", "desktop: set", "desktop: files", "desktop: store",
        "desktop: info", "desktop: game"
    };

    for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
        const char *Tag;

        gIcons[i].BmpReady = 0;
        if (!gIcons[i].Present || !gIcons[i].BmpPath) {
            continue;
        }
        if (i < DESKTOP_SYS_ICON_COUNT) {
            Tag = SysTags[i];
        } else {
            Tag = "desktop: app";
        }
        gIcons[i].BmpReady = LoadBmpPath(gIcons[i].BmpPath, &gIcons[i].Bmp,
                                         ICON_FILE_MAX, Tag);
    }
    gStartBmpReady = LoadBmpPath("Assets/Icons/bmp48/START.BMP", &gStartBmp,
                                 ICON_FILE_MAX, "desktop: start");
    gPowerBmpReady = LoadBmpPath("Assets/Icons/bmp48/POWER.BMP", &gPowerBmp,
                                 ICON_FILE_MAX, "desktop: power");
    gRebootBmpReady = LoadBmpPath("Assets/Icons/bmp48/REBOOT.BMP", &gRebootBmp,
                                  ICON_FILE_MAX, "desktop: reboot");
}

UINT32 BmpSampleScaled(const BMP_IMAGE *Img, UINT32 Dx, UINT32 Dy,
                              UINT32 Dw, UINT32 Dh) {
    UINT32 Sx;
    UINT32 Sy;

    if (!Img || !Img->Pixels || Img->Width == 0 || Img->Height == 0 ||
        Dw == 0 || Dh == 0) {
        return 0;
    }
    Sx = (Dx * Img->Width) / Dw;
    Sy = (Dy * Img->Height) / Dh;
    if (Sx >= Img->Width) {
        Sx = Img->Width - 1;
    }
    if (Sy >= Img->Height) {
        Sy = Img->Height - 1;
    }
    return Img->Pixels[Sy * Img->Width + Sx];
}

void BlitBmpScaledRaw(UINT32 X, UINT32 Y, UINT32 Dw, UINT32 Dh,
                             const BMP_IMAGE *Img) {
    UINT32 Row;
    UINT32 Col;
    UINT32 Line[64];

    if (!Img || !Img->Pixels || Dw == 0 || Dh == 0) {
        return;
    }
    if (Dw > 64) {
        Dw = 64;
    }
    for (Row = 0; Row < Dh; Row++) {
        for (Col = 0; Col < Dw; Col++) {
            Line[Col] = BmpSampleScaled(Img, Col, Row, Dw, Dh);
        }
        HalVideoWriteRect(X, Y + Row, Dw, 1, Line);
    }
}

/* BlitBmpScaledFree reserved if taskbar occlusion needs per-pixel later */

void BlitIconFaceRaw(UINT32 X, UINT32 Y, const DESKTOP_ICON *Icon) {
    UINT32 W;
    UINT32 H;
    UINT32 Row;

    if (Icon->BmpReady && Icon->Bmp.Pixels) {
        W = Icon->Bmp.Width;
        H = Icon->Bmp.Height;
        if (W > DESKTOP_ICON_SIZE) {
            W = DESKTOP_ICON_SIZE;
        }
        if (H > DESKTOP_ICON_SIZE) {
            H = DESKTOP_ICON_SIZE;
        }
        for (Row = 0; Row < H; Row++) {
            HalVideoWriteRect(X, Y + Row, W, 1,
                              &Icon->Bmp.Pixels[Row * Icon->Bmp.Width]);
        }
        return;
    }
    UiFillRectangle(X, Y, DESKTOP_ICON_SIZE, DESKTOP_ICON_SIZE, Icon->IconColor);
}

void BlitIconFaceFree(UINT32 X, UINT32 Y, const DESKTOP_ICON *Icon) {
    UINT32 W;
    UINT32 H;
    UINT32 Row;
    UINT32 Col;
    UINT32 RunStart;
    int InRun;

    if (Icon->BmpReady && Icon->Bmp.Pixels) {
        W = Icon->Bmp.Width;
        H = Icon->Bmp.Height;
        if (W > DESKTOP_ICON_SIZE) {
            W = DESKTOP_ICON_SIZE;
        }
        if (H > DESKTOP_ICON_SIZE) {
            H = DESKTOP_ICON_SIZE;
        }
        for (Row = 0; Row < H; Row++) {
            InRun = 0;
            RunStart = 0;
            for (Col = 0; Col < W; Col++) {
                int Free = !PointOccupied(X + Col, Y + Row);
                if (Free && !InRun) {
                    RunStart = Col;
                    InRun = 1;
                } else if (!Free && InRun) {
                    HalVideoWriteRect(X + RunStart, Y + Row, Col - RunStart, 1,
                                      &Icon->Bmp.Pixels[Row * Icon->Bmp.Width +
                                                        RunStart]);
                    InRun = 0;
                }
            }
            if (InRun) {
                HalVideoWriteRect(X + RunStart, Y + Row, W - RunStart, 1,
                                  &Icon->Bmp.Pixels[Row * Icon->Bmp.Width +
                                                    RunStart]);
            }
        }
        return;
    }
    FillRectFree(X, Y, DESKTOP_ICON_SIZE, DESKTOP_ICON_SIZE, Icon->IconColor);
}
