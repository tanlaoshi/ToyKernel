/*
 * VideoGop.c — PR-G-hotres-pc：真机 GOP 热切策略
 *
 * UEFI：ExitBootServices 后 Boot 协议（含 GOP SetMode）未定义。
 * NUC 实测：强行 SetMode → 先 #PF（缺映），映足后变成 #UD @ ip=0（固件跳空）。
 * 故真机**不做** live SetMode；Settings 写 THEME.CFG，下次 ToyBoot（EBS 前）切模式。
 * QEMU 热切走 Bochs，不进本文件。
 *
 * 仍保留 Map + Available：供「已是目标分辨率」时只同步内核 FB 视图，以及将来探测。
 */
#include "VideoPrivate.h"
#include "BootInfo.h"
#include "VirtualMemory.h"
#include "PhysicalMemory.h"
#include "Debug.h"
#include "HalConsole.h"

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
    /* UEFI GOP 为 MS ABI；仅 x86 真机路径可能用到约定，virt 编译勿告警告 */
#if defined(__x86_64__) || defined(_M_X64)
    UINT64 (__attribute__((ms_abi)) *SetMode)(TOY_GOP *This, UINT32 ModeNumber);
#else
    UINT64 (*SetMode)(TOY_GOP *This, UINT32 ModeNumber);
#endif
    void *Blt;
    TOY_GOP_MODE *Mode;
};

#define TOY_GOP_PIXEL_RGB 0u
#define TOY_GOP_PIXEL_BGR 1u

static int MapPhysRw(UINT64 Phys, UINTN Bytes) {
    UINT64 Start;
    UINT64 End;

    if (Phys == 0 || Bytes == 0) {
        return -1;
    }
    Start = Phys & ~((UINT64)PAGE_SIZE - 1ull);
    End = Phys + (UINT64)Bytes;
    if (End < Phys) {
        return -1;
    }
    End = (End + PAGE_SIZE - 1ull) & ~((UINT64)PAGE_SIZE - 1ull);
    return VirtualMemoryMapRange(Start, Start, (UINTN)(End - Start),
                                 PTE_PRESENT | PTE_WRITABLE);
}

static int MapPtrSpan(const void *Ptr, UINTN Span) {
    if (Ptr == 0) {
        return -1;
    }
    return MapPhysRw((UINT64)(UINTN)Ptr, Span);
}

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

/*
 * 已是目标分辨率：不调固件 SetMode，只按 GOP Mode 刷新内核 FB 视图。
 * 返回 0=已对齐；-1=读不到 Mode / 几何不符（调用方再走「拒绝热切」）。
 */
static int VideoGopSyncIfAlready(UINT32 Width, UINT32 Height) {
    const BOOT_INFO *Info;
    TOY_GOP *Gop;
    TOY_GOP_MODE *Mode;
    TOY_GOP_MODE_INFO *Mi;
    UINT64 Base;
    UINT64 Size;
    UINT64 MapBytes;
    UINT64 Need;
    VIDEO_CONFIG Cfg;
    UINT32 Pf;

    if (gPhysW == Width && gPhysH == Height && gScreen.FrameBufferBase != 0) {
        return 0;
    }

    Info = BootInfoGet();
    if (!Info || Info->GopProtocol == 0) {
        return -1;
    }
    Gop = (TOY_GOP *)(UINTN)Info->GopProtocol;
    if (MapPtrSpan(Gop, sizeof(TOY_GOP) + 0x1000) != 0 || !Gop->Mode) {
        return -1;
    }
    Mode = Gop->Mode;
    if (MapPtrSpan(Mode, sizeof(TOY_GOP_MODE) + 0x1000) != 0 || !Mode->Info) {
        return -1;
    }
    Mi = Mode->Info;
    if (MapPtrSpan(Mi, sizeof(TOY_GOP_MODE_INFO) + 0x1000) != 0) {
        return -1;
    }
    if (Mi->HorizontalResolution != Width || Mi->VerticalResolution != Height) {
        return -1;
    }
    Pf = Mi->PixelFormat;
    if (Pf != TOY_GOP_PIXEL_BGR && Pf != TOY_GOP_PIXEL_RGB) {
        return -1;
    }
    Base = Mode->FrameBufferBase;
    Size = (UINT64)Mode->FrameBufferSize;
    Need = (UINT64)Width * (UINT64)Height * 4ull;
    if (Base == 0 || Size < Need) {
        return -1;
    }
    MapBytes = Size;
    if (MapBytes < Need) {
        MapBytes = Need;
    }
    if (MapBytes < 16ull * 1024 * 1024) {
        MapBytes = 16ull * 1024 * 1024;
    }
    if (VirtualMemoryMapRange(Base, Base, (UINTN)MapBytes,
                              HalVideoFbMapFlags()) != 0) {
        return -1;
    }
    VideoReleaseBackbuffer();
    Cfg.FrameBufferBase = Base;
    Cfg.FrameBufferSize = Size;
    Cfg.HorizontalResolution = Width;
    Cfg.VerticalResolution = Height;
    Cfg.PixelsPerScanLine = Mi->PixelsPerScanLine;
    VideoSet(&Cfg);
    DebugWrite("video-gop: already at mode (no SetMode)\n");
    return 0;
}

int VideoGopSetMode(UINT32 Width, UINT32 Height) {
    UINT32 ModeNumber = 0;

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

    if (VideoGopSyncIfAlready(Width, Height) == 0) {
        return 0;
    }

    /*
     * 拒绝 EBS 后 live SetMode（NUC：#PF → 映页后 #UD@0）。
     * Settings 仍写 THEME；串口提示重启，ToyBoot 在 EBS 前切模式。
     */
    (void)ModeNumber;
    HalConsoleWriteSerial("video-gop: live SetMode refused (reboot for THEME)\n");
    DebugWrite("video-gop: live SetMode refused after EBS\n");
    return -1;
}
