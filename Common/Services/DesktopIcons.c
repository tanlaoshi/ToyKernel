/*
 * DesktopIcons.c — 图标 BMP / 布局 / 拖放移动（PR-S-desktop-split-3）
 *
 * 从 Desktop.c 迁出；只搬家、不改逻辑。
 */
#include "DesktopPriv.h"

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
    static const char *const Tags[DESKTOP_ICON_COUNT] = {
        "desktop: shell", "desktop: set", "desktop: files", "desktop: store"
    };

    for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
        gIcons[i].BmpReady = 0;
        if (!gIcons[i].BmpPath) {
            continue;
        }
        /* 失败则 BmpReady=0 → BlitIconFace* 画 IconColor 纯色 */
        gIcons[i].BmpReady = LoadBmpPath(gIcons[i].BmpPath, &gIcons[i].Bmp,
                                         ICON_FILE_MAX, Tags[i]);
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

static int ParseIconXy(const char *S, UINT32 *OutX, UINT32 *OutY) {
    UINT32 X = 0;
    UINT32 Y = 0;
    const char *P;

    if (!S || !OutX || !OutY) {
        return 0;
    }
    P = S;
    if (*P < '0' || *P > '9') {
        return 0;
    }
    while (*P >= '0' && *P <= '9') {
        X = X * 10u + (UINT32)(*P - '0');
        P++;
        if (X > 8192u) {
            return 0;
        }
    }
    if (*P != ',') {
        return 0;
    }
    P++;
    if (*P < '0' || *P > '9') {
        return 0;
    }
    while (*P >= '0' && *P <= '9') {
        Y = Y * 10u + (UINT32)(*P - '0');
        P++;
        if (Y > 8192u) {
            return 0;
        }
    }
    if (*P != '\0') {
        return 0;
    }
    *OutX = X;
    *OutY = Y;
    return 1;
}

static void FormatIconXy(UINT32 X, UINT32 Y, char *Out, UINTN OutMax) {
    char Tmp[16];
    UINTN N = 0;
    UINTN i;
    UINT32 V;

    if (!Out || OutMax < 4) {
        return;
    }
    V = X;
    if (V == 0) {
        Tmp[N++] = '0';
    } else {
        while (V > 0 && N < sizeof(Tmp)) {
            Tmp[N++] = (char)('0' + (V % 10));
            V /= 10;
        }
    }
    i = 0;
    while (N > 0 && i + 1 < OutMax) {
        Out[i++] = Tmp[--N];
    }
    if (i + 1 < OutMax) {
        Out[i++] = ',';
    }
    N = 0;
    V = Y;
    if (V == 0) {
        Tmp[N++] = '0';
    } else {
        while (V > 0 && N < sizeof(Tmp)) {
            Tmp[N++] = (char)('0' + (V % 10));
            V /= 10;
        }
    }
    while (N > 0 && i + 1 < OutMax) {
        Out[i++] = Tmp[--N];
    }
    Out[i] = '\0';
}

static const char *IconLayoutKey(int Idx) {
    switch (Idx) {
    case 0:
        return "ic0";
    case 1:
        return "ic1";
    case 2:
        return "ic2";
    case 3:
        return "ic3";
    default:
        return 0;
    }
}

/* PR-G-desk-1：从 TOYOS.DB 覆盖默认坐标 */
void LoadIconLayout(void) {
    int i;
    int Any = 0;
    char Val[DB_VAL_MAX];
    UINT32 X;
    UINT32 Y;
    const char *Key;

    for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
        Key = IconLayoutKey(i);
        if (!Key) {
            continue;
        }
        if (DbGet(Key, Val, sizeof(Val)) != DB_OK) {
            continue;
        }
        if (!ParseIconXy(Val, &X, &Y)) {
            continue;
        }
        ClampIconPos(&X, &Y);
        gIcons[i].X = X;
        gIcons[i].Y = Y;
        Any = 1;
    }
    if (Any) {
        DebugWrite("desktop: icon layout from TOYOS.DB\n");
    }
}

void SaveIconLayout(void) {
    int i;
    char Val[DB_VAL_MAX];
    const char *Key;
    int Ok = 1;

    DbBeginBatch();
    for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
        Key = IconLayoutKey(i);
        if (!Key) {
            continue;
        }
        FormatIconXy(gIcons[i].X, gIcons[i].Y, Val, sizeof(Val));
        if (DbSet(Key, Val) != DB_OK) {
            Ok = 0;
        }
    }
    if (DbEndBatch() != DB_OK) {
        Ok = 0;
    }
    if (Ok) {
        DebugWrite("desktop: icon layout saved\n");
    } else {
        DebugWrite("desktop: icon layout save failed\n");
    }
}

void PlaceDesktopIcons(void) {
    UINT32 RowH = DESKTOP_ICON_SIZE + DESKTOP_LABEL_PAD + FontCellH() +
                  DESKTOP_ICON_GAP;

    gIcons[0].Action = DESKTOP_ACTION_SHELL;
    gIcons[0].IconColor = COLOR_BLUE;
    gIcons[0].BmpPath = "Assets/Icons/bmp48/SHELL.BMP";
    gIcons[0].X = DESKTOP_ORIGIN_X;
    gIcons[0].Y = DESKTOP_ORIGIN_Y;

    gIcons[1].Action = DESKTOP_ACTION_SETTINGS;
    gIcons[1].IconColor = 0x00606080;
    gIcons[1].BmpPath = "Assets/Icons/bmp48/SET.BMP";
    gIcons[1].X = DESKTOP_ORIGIN_X;
    gIcons[1].Y = DESKTOP_ORIGIN_Y + RowH;

    gIcons[2].Action = DESKTOP_ACTION_FILES;
    gIcons[2].IconColor = 0x00208040;
    gIcons[2].BmpPath = "Assets/Icons/bmp48/FILES.BMP";
    gIcons[2].X = DESKTOP_ORIGIN_X;
    gIcons[2].Y = DESKTOP_ORIGIN_Y + RowH * 2;

    gIcons[3].Action = DESKTOP_ACTION_STORE;
    gIcons[3].IconColor = 0x002080C0;
    gIcons[3].BmpPath = "Assets/Icons/bmp48/STORE.BMP";
    gIcons[3].X = DESKTOP_ORIGIN_X;
    gIcons[3].Y = DESKTOP_ORIGIN_Y + RowH * 3;

    DesktopRefreshLabels();
}

void MoveIconTo(int Idx, UINT32 NewX, UINT32 NewY) {
    UINT32 Ox;
    UINT32 Oy;
    UINT32 Ow;
    UINT32 Oh;

    if (Idx < 0 || Idx >= DESKTOP_ICON_COUNT) {
        return;
    }
    ClampIconPos(&NewX, &NewY);
    if (gIcons[Idx].X == NewX && gIcons[Idx].Y == NewY) {
        return;
    }
    IconBounds(&gIcons[Idx], &Ox, &Oy, &Ow, &Oh);
    gIcons[Idx].X = NewX;
    gIcons[Idx].Y = NewY;
    DesktopFillRect(Ox, Oy, Ow, Oh);
    DesktopDrawRect(Ox, Oy, Ow, Oh);
    DrawOneIconOccluded(&gIcons[Idx], Idx == gDeskSelected);
    HalVideoPresent();
}
