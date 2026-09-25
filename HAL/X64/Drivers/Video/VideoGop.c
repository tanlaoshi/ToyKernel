/*
 * VideoGop.c — PR-G-hotres-pc：真机经 Boot 交接的 GOP SetMode 热切
 *
 * UEFI 规范上 ExitBootServices 后 Boot 协议未定义；部分固件仍可 SetMode。
 * QEMU 有 Bochs 时勿走本路径（GTK 环风险）。失败则 Settings 回落写盘+重启。
 */
#include "VideoPrivate.h"
#include "BootInfo.h"
#include "VirtualMemory.h"
#include "Debug.h"

/* 最小 GOP 布局（与 UEFI 一致；调用约定 MS ABI） */
typedef struct {
    UINT32 Version;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    UINT32 PixelFormat;
    UINT32 PixelInformation[4];
    UINT32 PixelsPerScanLine;
} TOY_GOP_MODE_INFO;

typedef struct {
    UINT32 MaxMode;
    UINT32 Mode;
    TOY_GOP_MODE_INFO *Info;
    UINTN SizeOfInfo;
    UINT64 FrameBufferBase;
    UINTN FrameBufferSize;
} TOY_GOP_MODE;

typedef struct TOY_GOP TOY_GOP;
struct TOY_GOP {
    void *QueryMode;
    UINT64 (__attribute__((ms_abi)) *SetMode)(TOY_GOP *This, UINT32 ModeNumber);
    void *Blt;
    TOY_GOP_MODE *Mode;
};

#define TOY_GOP_PIXEL_RGB 0u /* PixelRedGreenBlueReserved8BitPerColor */
#define TOY_GOP_PIXEL_BGR 1u /* PixelBlueGreenRedReserved8BitPerColor */

int VideoGopAvailable(void) {
    const BOOT_INFO *Info = BootInfoGet();

    if (!Info || Info->GopProtocol == 0 || Info->VideoModeCount == 0) {
        return 0;
    }
    if (!gFront || gScreen.FrameBufferBase == 0) {
        return 0;
    }
    return 1;
}

static int FindModeNumber(UINT32 Width, UINT32 Height, UINT32 *OutMode) {
    const BOOT_INFO *Info = BootInfoGet();
    UINT32 i;

    if (!Info || !OutMode) {
        return -1;
    }
    for (i = 0; i < Info->VideoModeCount && i < BOOT_VIDEO_MODE_MAX; i++) {
        if (Info->VideoModes[i].Width == Width &&
            Info->VideoModes[i].Height == Height) {
            *OutMode = Info->VideoModes[i].ModeNumber;
            return 0;
        }
    }
    return -1;
}

int VideoGopSetMode(UINT32 Width, UINT32 Height) {
    const BOOT_INFO *Info;
    TOY_GOP *Gop;
    TOY_GOP_MODE *Mode;
    TOY_GOP_MODE_INFO *Mi;
    UINT32 ModeNumber = 0;
    UINT64 Status;
    UINT64 Base;
    UINT64 Size;
    UINT64 MapBytes;
    VIDEO_CONFIG Cfg;
    UINT32 Pf;

    if (Width < 640 || Height < 480 || Width > 4096 || Height > 4096) {
        return -1;
    }
    if (!VideoGopAvailable()) {
        return -1;
    }
    if (FindModeNumber(Width, Height, &ModeNumber) != 0) {
        DebugWrite("video-gop: no mode for WxH\n");
        return -1;
    }

    Info = BootInfoGet();
    Gop = (TOY_GOP *)(UINTN)Info->GopProtocol;
    if (!Gop || !Gop->SetMode) {
        return -1;
    }

    Status = Gop->SetMode(Gop, ModeNumber);
    if (Status != 0) {
        DebugWrite("video-gop: SetMode failed\n");
        return -1;
    }

    Mode = Gop->Mode;
    if (!Mode || !Mode->Info) {
        DebugWrite("video-gop: Mode null after SetMode\n");
        return -1;
    }
    Mi = Mode->Info;
    Pf = Mi->PixelFormat;
    if (Pf != TOY_GOP_PIXEL_BGR && Pf != TOY_GOP_PIXEL_RGB) {
        DebugWrite("video-gop: unsupported pixel format\n");
        return -1;
    }
    if (Mi->HorizontalResolution != Width || Mi->VerticalResolution != Height) {
        DebugWrite("video-gop: SetMode geometry mismatch\n");
        return -1;
    }

    Base = Mode->FrameBufferBase;
    Size = (UINT64)Mode->FrameBufferSize;
    if (Base == 0 || Size < (UINT64)Width * (UINT64)Height * 4ull) {
        DebugWrite("video-gop: bad framebuffer\n");
        return -1;
    }

    MapBytes = Size;
    if (MapBytes < 16ull * 1024 * 1024) {
        MapBytes = 16ull * 1024 * 1024;
    }
    if (VirtualMemoryMapRange(Base, Base, (UINTN)MapBytes,
                              HalVideoFbMapFlags()) != 0) {
        DebugWrite("video-gop: map fb failed\n");
        return -1;
    }

    VideoReleaseBackbuffer();
    Cfg.FrameBufferBase = Base;
    Cfg.FrameBufferSize = Size;
    Cfg.HorizontalResolution = Width;
    Cfg.VerticalResolution = Height;
    Cfg.PixelsPerScanLine = Mi->PixelsPerScanLine;
    VideoSet(&Cfg);
    DebugWrite("video-gop: live SetMode ok\n");
    return 0;
}
