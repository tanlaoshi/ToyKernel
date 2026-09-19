/*
 * GuiFade.c — PR-GUI-l3-fade：开关窗淡入淡出中间帧
 *
 * 用窗备份与「无本窗」桌面合成层做 Src-over 插值；fade=0 跳过。
 * 不扩阴影（影在首/末帧随 Sync/Draw 出现），避免拖动路径复杂度。
 */
#include "GuiPrivate.h"
#include "HalVideo.h"
#include "Hal.h"
#include "PhysicalMemory.h"
#include "Theme.h"
#include "Desktop.h"

static void FadeFrameDelay(void) {
    UINTN N = HalCpuIsHypervisor() ? 40000u : 180000u;
    UINTN i;

    for (i = 0; i < N; i++) {
        HalCpuRelax();
    }
    HalTimerPoll();
}

/*
 * 不 Present：铺桌面 + 其它窗（含影），读本窗矩形为 under。
 * 调用方须已 ComposeBegin + 擦光标。
 */
static int CaptureUnderNoPresent(int Idx, UINT32 *Under, UINT32 Rw, UINT32 Rh) {
    const GUI_WINDOW *Win = &gWindows[Idx];
    int Saved = Win->Active;
    int i;

    gWindows[Idx].Active = 0;
    HalVideoClearClip();
    DesktopFillRect(0, 0, gScreenWidth, gScreenHeight);
    DesktopDraw();
    for (i = 0; i < MAX_WINS; i++) {
        if (!gWindows[i].Active) {
            continue;
        }
        if (gWinBackupValid[i] && gWinBackup[i] != 0) {
            PaintWindowFromBackup(i);
        } else {
            DrawWindowAtEx(i, WindowOccludedByOther(i) ? 1 : 0);
        }
    }
    for (i = 0; i < MAX_WINS; i++) {
        if (gWindows[i].Active) {
            DrawWindowShadowAt(i);
        }
    }
    HalVideoReadRect(Win->X, Win->Y, Rw, Rh, Under);
    gWindows[Idx].Active = Saved;
    return 1;
}

static void BlendFrame(UINT32 *Dst, const UINT32 *Under, const UINT32 *Over,
                       UINT32 N, UINT8 Alpha) {
    UINT32 i;

    if (Alpha == 0) {
        for (i = 0; i < N; i++) {
            Dst[i] = Under[i];
        }
        return;
    }
    if (Alpha == 255) {
        for (i = 0; i < N; i++) {
            Dst[i] = Over[i];
        }
        return;
    }
    for (i = 0; i < N; i++) {
        Dst[i] = HalVideoBlendRgb(Under[i], Over[i], Alpha);
    }
}

/*
 * FadeIn=1：备份已含不透明窗体，从透到实。
 * FadeIn=0：关窗前调用，窗仍 Active；结束后 FB 为本窗矩形 under（调用方再收尾）。
 */
void GuiAnimateWindowFade(int Idx, int FadeIn) {
    const GUI_WINDOW *Win;
    UINT32 Steps;
    UINT32 Rw;
    UINT32 Rh;
    UINT32 Pages;
    UINT32 *Under = 0;
    UINT32 *Frame = 0;
    UINT32 Npix;
    UINT32 Step;
    UINT8 Alpha;
    int Ok = 0;

    Steps = ThemeWindowFadeSteps();
    if (Steps == 0 || Idx < 0 || Idx >= MAX_WINS) {
        return;
    }
    Win = &gWindows[Idx];
    if (!Win->Active || Win->Width == 0 || Win->Height == 0) {
        return;
    }
    if (!gWinBackupValid[Idx] || gWinBackup[Idx] == 0) {
        BackupWindowAt(Idx);
        if (!gWinBackupValid[Idx] || gWinBackup[Idx] == 0) {
            return;
        }
    }

    Rw = gWinBackupW[Idx];
    Rh = gWinBackupH[Idx];
    if (Rw == 0 || Rh == 0) {
        return;
    }
    if (Win->X + Rw > gScreenWidth) {
        Rw = gScreenWidth - Win->X;
    }
    if (Win->Y + Rh > gScreenHeight) {
        Rh = gScreenHeight - Win->Y;
    }
    if (Rw == 0 || Rh == 0) {
        return;
    }

    Pages = BackupPageCount(Rw, Rh);
    Under = (UINT32 *)PhysicalMemoryAllocatePages(Pages);
    Frame = (UINT32 *)PhysicalMemoryAllocatePages(Pages);
    if (Under == 0 || Frame == 0) {
        goto done;
    }

    ComposeBegin();
    GfxIrqEnter();
    CursorRestore();
    GfxIrqLeave();

    if (!CaptureUnderNoPresent(Idx, Under, Rw, Rh)) {
        ComposeEnd();
        goto done;
    }

    Npix = Rw * Rh;
    if (FadeIn) {
        for (Step = 1; Step <= Steps; Step++) {
            Alpha = (UINT8)((Step * 255u) / Steps);
            BlendFrame(Frame, Under, gWinBackup[Idx], Npix, Alpha);
            HalVideoWriteRect(Win->X, Win->Y, Rw, Rh, Frame);
            GfxIrqEnter();
            HalVideoPresent();
            GfxIrqLeave();
            if (Step < Steps) {
                FadeFrameDelay();
            }
        }
        HalVideoWriteRect(Win->X, Win->Y, Rw, Rh, gWinBackup[Idx]);
        DrawWindowChromeAt(Idx);
        DrawWindowShadowAt(Idx);
    } else {
        for (Step = Steps; Step >= 1; Step--) {
            Alpha = (UINT8)((Step * 255u) / Steps);
            BlendFrame(Frame, Under, gWinBackup[Idx], Npix, Alpha);
            HalVideoWriteRect(Win->X, Win->Y, Rw, Rh, Frame);
            GfxIrqEnter();
            HalVideoPresent();
            GfxIrqLeave();
            if (Step > 1) {
                FadeFrameDelay();
            }
        }
        HalVideoWriteRect(Win->X, Win->Y, Rw, Rh, Under);
    }

    GfxIrqEnter();
    CursorPaint();
    HalVideoPresent();
    GfxIrqLeave();
    ComposeEnd();
    Ok = 1;

done:
    (void)Ok;
    if (Under != 0) {
        PhysicalMemoryFreePages(Under, Pages);
    }
    if (Frame != 0) {
        PhysicalMemoryFreePages(Frame, Pages);
    }
}
