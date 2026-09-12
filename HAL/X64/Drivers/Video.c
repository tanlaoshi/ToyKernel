/*
 * Video.c — GOP 帧缓冲驱动（PR-G9：可选 backbuffer + 脏矩形 Present）
 *
 * 绘制写入后缓冲（若已启用）；VideoPresent 将脏区一次 blit 到 scanout。
 * PR-G-present：脏区按行 memcpy（非整段逐像素）。
 * 字形经 Font_*（Fonts/），不直接绑定某一份点阵表。
 */
#include "Video.h"
#include "Font.h"
#include "PhysicalMemory.h"
#include "Hal.h"

extern void *memcpy(void *Dst, const void *Src, UINTN Len);

/* Bochs/QEMU VBE DISPI（OVMF QemuVideo 同端口） */
#define VBE_DISPI_IOPORT_INDEX  0x01CE
#define VBE_DISPI_IOPORT_DATA   0x01D0
#define VBE_DISPI_INDEX_ID      0x0
#define VBE_DISPI_INDEX_XRES    0x1
#define VBE_DISPI_INDEX_YRES    0x2
#define VBE_DISPI_INDEX_BPP     0x3
#define VBE_DISPI_INDEX_ENABLE  0x4
#define VBE_DISPI_INDEX_BANK    0x5
#define VBE_DISPI_INDEX_VIRT_WIDTH  0x6
#define VBE_DISPI_INDEX_VIRT_HEIGHT 0x7
#define VBE_DISPI_INDEX_X_OFFSET    0x8
#define VBE_DISPI_INDEX_Y_OFFSET    0x9
#define VBE_DISPI_INDEX_VIDEO_MEMORY_64K 0xa
#define VBE_DISPI_ID0           0xB0C0
#define VBE_DISPI_ENABLED       0x01
#define VBE_DISPI_LFB_ENABLED   0x40

static SCREEN_INFO gScreen = {0};
static UINT32 gBackground = 0x00000000;
static int gClipOn;
static UINT32 gClipX;
static UINT32 gClipY;
static UINT32 gClipW;
static UINT32 gClipH;
static UINT32 gClipBg;

/* scanout（GOP）与后缓冲 */
static UINT32 *gFront;
static UINT32  gFrontPitch;
static UINT32 *gBack;
static UINT32  gBackPitch;
static UINT32  gBackPages;
static int     gBackOn;
/* 真机 boot mark：直写 scanout，避开后缓冲 Present 假死 */
static int     gForceFront;

/* 内容脏矩形 [gDx0,gDx1) x [gDy0,gDy1)；光标 XOR 单独跟踪，避免 AABB 并成近全屏 */
static int     gDirty;
static UINT32  gDx0;
static UINT32  gDy0;
static UINT32  gDx1;
static UINT32  gDy1;
static int     gCurDirty;
static UINT32  gCx0;
static UINT32  gCy0;
static UINT32  gCx1;
static UINT32  gCy1;
/* 光标叠层绘制：DirtyUnion 改记光标矩形 */
static int     gCursorOverlay;

/* 单次 Present 条带行数：cli 下 memcpy 过久会饿死 xHCI poll/MSI */
#define PRESENT_CHUNK_ROWS 64u

static void DirtyUnionInto(int *Dirty, UINT32 *Dx0, UINT32 *Dy0, UINT32 *Dx1,
                           UINT32 *Dy1, UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    UINT32 X1;
    UINT32 Y1;

    if (gForceFront) {
        return;
    }
    if (!W || !H || gScreen.Width == 0 || gScreen.Height == 0) {
        return;
    }
    if (X >= gScreen.Width || Y >= gScreen.Height) {
        return;
    }
    X1 = X + W;
    Y1 = Y + H;
    if (X1 > gScreen.Width) {
        X1 = gScreen.Width;
    }
    if (Y1 > gScreen.Height) {
        Y1 = gScreen.Height;
    }
    if (X >= X1 || Y >= Y1) {
        return;
    }
    if (!*Dirty) {
        *Dx0 = X;
        *Dy0 = Y;
        *Dx1 = X1;
        *Dy1 = Y1;
        *Dirty = 1;
        return;
    }
    if (X < *Dx0) {
        *Dx0 = X;
    }
    if (Y < *Dy0) {
        *Dy0 = Y;
    }
    if (X1 > *Dx1) {
        *Dx1 = X1;
    }
    if (Y1 > *Dy1) {
        *Dy1 = Y1;
    }
}

static void DirtyUnion(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    if (gCursorOverlay) {
        DirtyUnionInto(&gCurDirty, &gCx0, &gCy0, &gCx1, &gCy1, X, Y, W, H);
        return;
    }
    DirtyUnionInto(&gDirty, &gDx0, &gDy0, &gDx1, &gDy1, X, Y, W, H);
}

static void DirtyUnionCursor(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    DirtyUnionInto(&gCurDirty, &gCx0, &gCy0, &gCx1, &gCy1, X, Y, W, H);
}

void VideoCursorOverlayBegin(void) {
    gCursorOverlay = 1;
}

void VideoCursorOverlayEnd(void) {
    gCursorOverlay = 0;
}

static UINT32 *DrawBase(void) {
    if (gForceFront && gFront) {
        return gFront;
    }
    return gBackOn ? gBack : gFront;
}

static UINT32 DrawPitch(void) {
    if (gForceFront && gFront) {
        return gFrontPitch;
    }
    return gBackOn ? gBackPitch : gFrontPitch;
}

void VideoDrawBeginFront(void) {
    gForceFront = 1;
}

void VideoDrawEndFront(void) {
    gForceFront = 0;
}

void VideoSet(VIDEO_CONFIG *VideoConfig) {
    gScreen.Width = VideoConfig->HorizontalResolution;
    gScreen.Height = VideoConfig->VerticalResolution;
    gScreen.PixelsPerScanLine = VideoConfig->PixelsPerScanLine;
    gScreen.FrameBufferBase = VideoConfig->FrameBufferBase;
    gScreen.FrameBufferSize = VideoConfig->FrameBufferSize;
    gScreen.CursorX = 0;
    gScreen.CursorY = 0;
    gFront = (UINT32 *)(UINTN)VideoConfig->FrameBufferBase;
    gFrontPitch = VideoConfig->PixelsPerScanLine;
    gBack = 0;
    gBackPitch = 0;
    gBackPages = 0;
    gBackOn = 0;
    gDirty = 0;
    gCurDirty = 0;
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

static void BochsWrite(UINT16 Index, UINT16 Value) {
    HalIoWrite16(VBE_DISPI_IOPORT_INDEX, Index);
    HalIoWrite16(VBE_DISPI_IOPORT_DATA, Value);
}

static UINT16 BochsRead(UINT16 Index) {
    HalIoWrite16(VBE_DISPI_IOPORT_INDEX, Index);
    return HalIoRead16(VBE_DISPI_IOPORT_DATA);
}

static int BochsPresent(void) {
    UINT16 Id = BochsRead(VBE_DISPI_INDEX_ID);
    return (Id & 0xFFF0u) == VBE_DISPI_ID0;
}

int VideoBochsAvailable(void) {
    if (!gFront || gScreen.FrameBufferBase == 0) {
        return 0;
    }
    return BochsPresent();
}

/*
 * PR-G-hotres：经 Bochs DISPI 改 scanout 几何；LFB 基址沿用 Boot GOP。
 * 调用方须已映射足够大的帧缓冲，并在成功后重配后缓冲 / Gui。
 */
int VideoBochsSetMode(UINT32 Width, UINT32 Height) {
    UINT64 Need;
    UINT16 Mem64k;
    UINT64 Vram;
    VIDEO_CONFIG Cfg;

    if (Width < 320 || Height < 200 || Width > 4096 || Height > 4096) {
        return -1;
    }
    if (!gFront || gScreen.FrameBufferBase == 0) {
        return -1;
    }
    if (!BochsPresent()) {
        return -1;
    }

    Need = (UINT64)Width * (UINT64)Height * sizeof(UINT32);
    Mem64k = BochsRead(VBE_DISPI_INDEX_VIDEO_MEMORY_64K);
    if (Mem64k != 0) {
        Vram = (UINT64)Mem64k * 65536ull;
    } else if (gScreen.FrameBufferSize >= Need) {
        Vram = gScreen.FrameBufferSize;
    } else {
        Vram = 16ull * 1024 * 1024; /* QEMU VGA 常见默认 */
    }
    if (Vram < Need) {
        return -1;
    }

    BochsWrite(VBE_DISPI_INDEX_ENABLE, 0);
    BochsWrite(VBE_DISPI_INDEX_BANK, 0);
    BochsWrite(VBE_DISPI_INDEX_X_OFFSET, 0);
    BochsWrite(VBE_DISPI_INDEX_Y_OFFSET, 0);
    BochsWrite(VBE_DISPI_INDEX_BPP, 32);
    BochsWrite(VBE_DISPI_INDEX_XRES, (UINT16)Width);
    BochsWrite(VBE_DISPI_INDEX_YRES, (UINT16)Height);
    BochsWrite(VBE_DISPI_INDEX_VIRT_WIDTH, (UINT16)Width);
    BochsWrite(VBE_DISPI_INDEX_VIRT_HEIGHT, (UINT16)Height);
    BochsWrite(VBE_DISPI_INDEX_ENABLE, (UINT16)(VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED));

    /* 读回 DISPI，避免“软件记成 WxH、硬件仍是旧模式” */
    if (BochsRead(VBE_DISPI_INDEX_XRES) != (UINT16)Width ||
        BochsRead(VBE_DISPI_INDEX_YRES) != (UINT16)Height) {
        return -1;
    }

    VideoReleaseBackbuffer();
    Cfg.FrameBufferBase = gScreen.FrameBufferBase;
    Cfg.FrameBufferSize = Need;
    Cfg.HorizontalResolution = Width;
    Cfg.VerticalResolution = Height;
    Cfg.PixelsPerScanLine = Width;
    VideoSet(&Cfg);
    return 0;
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
     * 不从 GOP 全屏拷：InitVideo 紧接着 ClearScreen+Present。
     * 后缓冲内容以后续绘制为准。
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

/* 将脏区 blit 到 GOP；无后缓冲时为空操作 */
static void PresentRectRows(UINT32 X0, UINT32 Y0, UINT32 X1, UINT32 Y1,
                            UINT64 FbBytes, int *DirtyOut, UINT32 *Dx0,
                            UINT32 *Dy0, UINT32 *Dx1, UINT32 *Dy1,
                            int *OutPartial) {
    UINT32 Y;
    UINT32 ChunkEnd;
    UINT64 RowOff;
    UINT64 RowBytes;
    UINT64 Flags;

    *OutPartial = 0;
    if (X0 >= X1 || Y0 >= Y1) {
        return;
    }
    RowBytes = (UINT64)(X1 - X0) * 4ull;
    Y = Y0;
    while (Y < Y1) {
        ChunkEnd = Y + PRESENT_CHUNK_ROWS;
        if (ChunkEnd > Y1) {
            ChunkEnd = Y1;
        }
        Flags = HalIrqSave();
        for (; Y < ChunkEnd; Y++) {
            const UINT32 *Src;
            UINT32 *Dst;

            RowOff = ((UINT64)Y * (UINT64)gFrontPitch + (UINT64)X0) * 4ull;
            if (RowOff + RowBytes > FbBytes) {
                /* 其余行仍脏，下次 Present 续传 */
                *Dx0 = X0;
                *Dy0 = Y;
                *Dx1 = X1;
                *Dy1 = Y1;
                *DirtyOut = 1;
                *OutPartial = 1;
                HalIrqRestore(Flags);
                return;
            }
            Src = &gBack[Y * gBackPitch + X0];
            Dst = &gFront[Y * gFrontPitch + X0];
            memcpy(Dst, Src, (UINTN)RowBytes);
        }
        HalIrqRestore(Flags);
        /* 条带间隙开中断，让 xHCI MSI/软轮询有机会 Drain */
    }
}

void VideoPresent(void) {
    UINT32 X0;
    UINT32 Y0;
    UINT32 X1;
    UINT32 Y1;
    UINT64 FbBytes;
    UINT64 LayoutBytes;
    int Partial;

    if (!gBackOn || !gBack || !gFront) {
        gDirty = 0;
        gCurDirty = 0;
        return;
    }
    if (!gDirty && !gCurDirty) {
        return;
    }
    /*
     * 内容与光标分矩形 Present，避免 AABB 并成近全屏。
     * 大块按行条带 cli，条间开中断（G7：禁止长 cli 饿死 USB）。
     */
    LayoutBytes = (UINT64)gFrontPitch * (UINT64)gScreen.Height * 4ull;
    FbBytes = gScreen.FrameBufferSize;
    if (FbBytes == 0) {
        FbBytes = LayoutBytes;
    } else if (gFrontPitch > gScreen.Width &&
               FbBytes == (UINT64)gScreen.Width * (UINT64)gScreen.Height * 4ull &&
               LayoutBytes > FbBytes) {
        FbBytes = LayoutBytes;
    }

    if (gDirty) {
        X0 = gDx0;
        Y0 = gDy0;
        X1 = gDx1;
        Y1 = gDy1;
        gDirty = 0;
        if (X0 < gScreen.Width && Y0 < gScreen.Height) {
            if (X1 > gScreen.Width) {
                X1 = gScreen.Width;
            }
            if (Y1 > gScreen.Height) {
                Y1 = gScreen.Height;
            }
            PresentRectRows(X0, Y0, X1, Y1, FbBytes, &gDirty, &gDx0, &gDy0,
                            &gDx1, &gDy1, &Partial);
            if (Partial) {
                return;
            }
        }
    }

    if (gCurDirty) {
        X0 = gCx0;
        Y0 = gCy0;
        X1 = gCx1;
        Y1 = gCy1;
        gCurDirty = 0;
        if (X0 < gScreen.Width && Y0 < gScreen.Height) {
            if (X1 > gScreen.Width) {
                X1 = gScreen.Width;
            }
            if (Y1 > gScreen.Height) {
                Y1 = gScreen.Height;
            }
            PresentRectRows(X0, Y0, X1, Y1, FbBytes, &gCurDirty, &gCx0, &gCy0,
                            &gCx1, &gCy1, &Partial);
        }
    }
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

void VideoDrawCharAt(UINT32 X, UINT32 Y, char C, UINT32 Color) {
    const FONT_FACE *F;
    const UINT8 *Glyph;
    UINT32 Scale;
    UINT32 Row;
    UINT32 Col;
    UINT32 Sy;
    UINT32 Sx;

    F = FontGetCurrent();
    Glyph = FontGlyph(C);
    if (F == 0 || Glyph == 0) {
        return;
    }
    Scale = F->Scale ? F->Scale : 1u;

    for (Row = 0; Row < F->Height; Row++) {
        for (Col = 0; Col < F->Width; Col++) {
            UINT8 Byte = Glyph[Row * F->BytesPerRow + (Col / 8)];
            int Bit = 7 - (int)(Col % 8);
            if ((Byte & (1 << Bit)) == 0) {
                continue;
            }
            for (Sy = 0; Sy < Scale; Sy++) {
                for (Sx = 0; Sx < Scale; Sx++) {
                    VideoDrawPixel(
                        X + Col * Scale + Sx,
                        Y + Row * Scale + Sy,
                        Color);
                }
            }
        }
    }
}

/* 任意点阵：BytesPerRow = (Width+7)/8；CJK 短于行高时 PR-T1 拉伸至 FontCellH */
static void VideoDrawBitmapAt(UINT32 X, UINT32 Y, const UINT8 *Glyph,
                              UINT32 Width, UINT32 Height, UINT32 Color) {
    UINT32 ScaleX;
    UINT32 ScaleY;
    UINT32 Bpr;
    UINT32 Row;
    UINT32 Col;
    UINT32 Sy;
    UINT32 Sx;
    UINT32 CellH;
    UINT32 OffY;
    UINT32 DrawnH;

    if (!Glyph || Width == 0 || Height == 0) {
        return;
    }
    ScaleY = FontGlyphStretch(Height);
    ScaleX = ScaleY; /* 方形拉伸；与英文同高 */
    if (ScaleX < 1) {
        ScaleX = 1;
    }
    if (ScaleY < 1) {
        ScaleY = 1;
    }
    CellH = FontCellH();
    DrawnH = Height * ScaleY;
    OffY = 0;
    /* 整数拉伸凑不满行高时（如 10×18 下 CJK 16）垂直居中 */
    if (CellH > DrawnH) {
        OffY = (CellH - DrawnH) / 2;
    }
    Bpr = (Width + 7) / 8;

    for (Row = 0; Row < Height; Row++) {
        for (Col = 0; Col < Width; Col++) {
            UINT8 Byte = Glyph[Row * Bpr + (Col / 8)];
            int Bit = 7 - (int)(Col % 8);
            if ((Byte & (1 << Bit)) == 0) {
                continue;
            }
            for (Sy = 0; Sy < ScaleY; Sy++) {
                for (Sx = 0; Sx < ScaleX; Sx++) {
                    VideoDrawPixel(
                        X + Col * ScaleX + Sx,
                        Y + OffY + Row * ScaleY + Sy,
                        Color);
                }
            }
        }
    }
}

void VideoDrawCodepointAt(UINT32 X, UINT32 Y, UINT32 Cp, UINT32 Color) {
    UINT32 W;
    UINT32 H;
    const UINT8 *G;

    if (Cp < 128) {
        VideoDrawCharAt(X, Y, (char)Cp, Color);
        return;
    }
    G = FontGlyphCp(Cp, &W, &H);
    if (!G) {
        return;
    }
    VideoDrawBitmapAt(X, Y, G, W, H, Color);
}

static UINT32 CodepointAdvance(UINT32 Cp) {
    return FontCodepointAdvance(Cp);
}

void VideoDrawStringAt(UINT32 X, UINT32 Y, const char *Text, UINT32 Color) {
    UINT32 CurX = X;
    UINT32 AdvY = FontAdvanceY();

    while (Text && *Text) {
        UINT32 Cp;
        UINTN N;

        if (*Text == '\n') {
            CurX = X;
            Y += AdvY;
            Text++;
            continue;
        }
        N = Utf8Decode(Text, &Cp);
        if (N == 0) {
            Text++;
            continue;
        }
        VideoDrawCodepointAt(CurX, Y, Cp, Color);
        CurX += CodepointAdvance(Cp);
        Text += N;
    }
}

void VideoDrawPixelRaw(UINT32 X, UINT32 Y, UINT32 Color) {
    UINT32 *Fb;
    UINT32 Pitch;

    if (X >= gScreen.Width || Y >= gScreen.Height) {
        return;
    }
    Fb = DrawBase();
    Pitch = DrawPitch();
    if (!Fb || Pitch == 0) {
        return;
    }
    Fb[Y * Pitch + X] = Color;
    DirtyUnion(X, Y, 1, 1);
}

/* 光标 XOR：再异或一次即擦除，无需 save-under/ReadPixel */
void VideoXorPixelRaw(UINT32 X, UINT32 Y, UINT32 Mask) {
    UINT32 *Fb;
    UINT32 Pitch;

    if (X >= gScreen.Width || Y >= gScreen.Height) {
        return;
    }
    Fb = DrawBase();
    Pitch = DrawPitch();
    if (!Fb || Pitch == 0) {
        return;
    }
    Fb[Y * Pitch + X] ^= Mask;
    /* 勿 DirtyUnion 进内容脏区：4K 下 Shell∪远处光标会并成近全屏 Present */
    DirtyUnionCursor(X, Y, 1, 1);
}

void VideoDrawPixel(UINT32 X, UINT32 Y, UINT32 Color) {
    if (gClipOn) {
        if (X < gClipX || Y < gClipY ||
            X >= gClipX + gClipW || Y >= gClipY + gClipH) {
            return;
        }
    }
    VideoDrawPixelRaw(X, Y, Color);
}

UINT32 VideoReadPixel(UINT32 X, UINT32 Y) {
    UINT32 *Fb;
    UINT32 Pitch;

    if (X >= gScreen.Width || Y >= gScreen.Height) {
        return 0;
    }
    Fb = DrawBase();
    Pitch = DrawPitch();
    if (!Fb || Pitch == 0) {
        return 0;
    }
    return Fb[Y * Pitch + X];
}

void VideoFillRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color) {
    UINT32 *Fb;
    UINT32 Pitch;
    UINT32 Row;
    UINT32 Col;
    UINT32 CopyW;
    UINT32 CopyH;
    UINT32 X0;
    UINT32 Y0;

    if (!Width || !Height) {
        return;
    }
    Fb = DrawBase();
    Pitch = DrawPitch();
    if (!Fb || Pitch == 0) {
        return;
    }
    X0 = X;
    Y0 = Y;
    CopyW = Width;
    CopyH = Height;

    /* PR-G10 M5：与 DrawPixel 一致，尊重客户区 clip */
    if (gClipOn) {
        UINT32 ClipR = gClipX + gClipW;
        UINT32 ClipB = gClipY + gClipH;
        UINT32 R;
        UINT32 B;

        if (X0 >= ClipR || Y0 >= ClipB) {
            return;
        }
        if (X0 < gClipX) {
            UINT32 Skip = gClipX - X0;
            if (Skip >= CopyW) {
                return;
            }
            CopyW -= Skip;
            X0 = gClipX;
        }
        if (Y0 < gClipY) {
            UINT32 Skip = gClipY - Y0;
            if (Skip >= CopyH) {
                return;
            }
            CopyH -= Skip;
            Y0 = gClipY;
        }
        if (CopyW == 0 || CopyH == 0) {
            return;
        }
        R = X0 + CopyW;
        B = Y0 + CopyH;
        if (R > ClipR) {
            CopyW = ClipR - X0;
        }
        if (B > ClipB) {
            CopyH = ClipB - Y0;
        }
    }

    if (X0 >= gScreen.Width || Y0 >= gScreen.Height) {
        return;
    }
    if (X0 + CopyW > gScreen.Width) {
        CopyW = gScreen.Width - X0;
    }
    if (Y0 + CopyH > gScreen.Height) {
        CopyH = gScreen.Height - Y0;
    }
    if (CopyW == 0 || CopyH == 0) {
        return;
    }
    for (Row = 0; Row < CopyH; Row++) {
        UINT32 *Line = &Fb[(Y0 + Row) * Pitch + X0];

        for (Col = 0; Col < CopyW; Col++) {
            Line[Col] = Color;
        }
    }
    DirtyUnion(X0, Y0, CopyW, CopyH);
}

void VideoCopyRect(UINT32 SrcX, UINT32 SrcY, UINT32 DstX, UINT32 DstY,
                   UINT32 Width, UINT32 Height) {
    UINT32 *Fb = DrawBase();
    UINT32 Pitch = DrawPitch();
    INT32 Y;
    INT32 X;
    INT32 W;
    INT32 H;
    UINT32 CopyW = Width;
    UINT32 CopyH = Height;

    if (!Fb || !Width || !Height || Pitch == 0) {
        return;
    }
    if (SrcX >= gScreen.Width || SrcY >= gScreen.Height ||
        DstX >= gScreen.Width || DstY >= gScreen.Height) {
        return;
    }
    if (SrcX + CopyW > gScreen.Width) {
        CopyW = gScreen.Width - SrcX;
    }
    if (DstX + CopyW > gScreen.Width) {
        CopyW = gScreen.Width - DstX;
    }
    if (SrcY + CopyH > gScreen.Height) {
        CopyH = gScreen.Height - SrcY;
    }
    if (DstY + CopyH > gScreen.Height) {
        CopyH = gScreen.Height - DstY;
    }
    W = (INT32)CopyW;
    H = (INT32)CopyH;
    if (W <= 0 || H <= 0) {
        return;
    }

    /* PR-G10 L5：按行拷贝（同向/逆向处理重叠） */
    if (DstY > SrcY || (DstY == SrcY && DstX > SrcX)) {
        for (Y = H - 1; Y >= 0; Y--) {
            UINT32 *Dst = &Fb[(DstY + (UINT32)Y) * Pitch + DstX];
            UINT32 *Src = &Fb[(SrcY + (UINT32)Y) * Pitch + SrcX];
            for (X = W - 1; X >= 0; X--) {
                Dst[X] = Src[X];
            }
        }
    } else if (DstY != SrcY || DstX != SrcX) {
        for (Y = 0; Y < H; Y++) {
            UINT32 *Dst = &Fb[(DstY + (UINT32)Y) * Pitch + DstX];
            UINT32 *Src = &Fb[(SrcY + (UINT32)Y) * Pitch + SrcX];
            for (X = 0; X < W; X++) {
                Dst[X] = Src[X];
            }
        }
    }
    DirtyUnion(DstX, DstY, (UINT32)W, (UINT32)H);
}

void VideoReadRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 *Out) {
    UINT32 *Fb = DrawBase();
    UINT32 Pitch = DrawPitch();
    UINT32 Row;
    UINT32 Col;
    UINT32 i = 0;

    if (!Fb || !Out || !Width || !Height || Pitch == 0) {
        return;
    }
    if (X >= gScreen.Width || Y >= gScreen.Height) {
        return;
    }
    if (X + Width > gScreen.Width) {
        Width = gScreen.Width - X;
    }
    if (Y + Height > gScreen.Height) {
        Height = gScreen.Height - Y;
    }
    for (Row = 0; Row < Height; Row++) {
        for (Col = 0; Col < Width; Col++) {
            Out[i++] = Fb[(Y + Row) * Pitch + X + Col];
        }
    }
}

void VideoWriteRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const UINT32 *In) {
    UINT32 *Fb = DrawBase();
    UINT32 Pitch = DrawPitch();
    UINT32 Row;
    UINT32 SrcStride;
    UINT32 CopyW;
    UINT32 CopyH;

    if (!Fb || !In || !Width || !Height || Pitch == 0) {
        return;
    }
    if (X >= gScreen.Width || Y >= gScreen.Height) {
        return;
    }
    SrcStride = Width;
    CopyW = Width;
    CopyH = Height;
    if (X + CopyW > gScreen.Width) {
        CopyW = gScreen.Width - X;
    }
    if (Y + CopyH > gScreen.Height) {
        CopyH = gScreen.Height - Y;
    }
    for (Row = 0; Row < CopyH; Row++) {
        UINT32 *Dst = &Fb[(Y + Row) * Pitch + X];
        const UINT32 *Src = &In[Row * SrcStride];
        UINT32 Col;

        for (Col = 0; Col < CopyW; Col++) {
            Dst[Col] = Src[Col];
        }
    }
    DirtyUnion(X, Y, CopyW, CopyH);
}

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
    if ((UINT8)c < 32 || (UINT8)c > 126) {
        return;
    }
    VideoDrawCodepoint(c, Color);
}

void VideoDrawCodepoint(UINT32 Cp, UINT32 Color) {
    UINT32 MaxX;
    UINT32 MaxY;
    UINT32 Adv;

    if (Cp == 0 || Cp == '\n') {
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
