/*
 * DesktopBootLogo.c — PR-BOX-2：启动占位 Logo → 清屏 → 再进 DesktopInit
 * T=锤、O=足球、Y=弹弓；旁衬 ToyOS。不做动画引擎。
 */
#include "Desktop.h"
#include "Hal.h"
#include "HalVideo.h"
#include "ToySerialLog.h"

#define LOGO_BG     0x00101828u
#define LOGO_FG     0x00E8EEF4u
#define LOGO_ACCENT 0x00F0A030u
#define LOGO_BALL   0x00C87830u
#define LOGO_WOOD   0x008B5A2Bu
#define LOGO_STEEL  0x00A0A8B0u

static void Fill(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, UINT32 C) {
    if (W == 0 || H == 0) {
        return;
    }
    HalVideoFillRect(X, Y, W, H, C);
}

/* 简易实心圆（粗像素） */
static void Disk(UINT32 Cx, UINT32 Cy, UINT32 R, UINT32 C) {
    UINT32 Y;
    UINT32 X;
    UINT32 R2 = R * R;

    for (Y = 0; Y <= 2 * R; Y++) {
        for (X = 0; X <= 2 * R; X++) {
            INT32 Dx = (INT32)X - (INT32)R;
            INT32 Dy = (INT32)Y - (INT32)R;
            if ((UINT32)(Dx * Dx + Dy * Dy) <= R2) {
                HalVideoDrawPixel(Cx + X - R, Cy + Y - R, C);
            }
        }
    }
}

static void DrawHammer(UINT32 X, UINT32 Y) {
    Fill(X + 28, Y + 8, 12, 56, LOGO_WOOD);
    Fill(X + 8, Y + 4, 52, 18, LOGO_STEEL);
    Fill(X + 4, Y + 8, 8, 10, LOGO_STEEL);
    Fill(X + 56, Y + 8, 8, 10, LOGO_STEEL);
}

static void DrawFootball(UINT32 X, UINT32 Y) {
    UINT32 Cx = X + 32;
    UINT32 Cy = Y + 36;
    Disk(Cx, Cy, 28, LOGO_BALL);
    Fill(Cx - 3, Cy - 22, 6, 44, LOGO_FG);
    Fill(Cx - 10, Cy - 4, 8, 4, LOGO_FG);
    Fill(Cx + 2, Cy - 4, 8, 4, LOGO_FG);
    Fill(Cx - 10, Cy + 8, 8, 4, LOGO_FG);
    Fill(Cx + 2, Cy + 8, 8, 4, LOGO_FG);
}

static void DrawSlingshot(UINT32 X, UINT32 Y) {
    Fill(X + 28, Y + 40, 10, 28, LOGO_WOOD);
    Fill(X + 8, Y + 8, 10, 40, LOGO_WOOD);
    Fill(X + 48, Y + 8, 10, 40, LOGO_WOOD);
    Fill(X + 14, Y + 6, 38, 6, LOGO_ACCENT);
    Fill(X + 18, Y + 20, 30, 4, LOGO_ACCENT);
}

static void LogoHold(void) {
    UINTN Round;
    volatile UINTN I;

    /* ~可视一拍；不挡 smoke（远小于 90s） */
    for (Round = 0; Round < 30; Round++) {
        HalInputPoll();
        for (I = 0; I < 1500000UL; I++) {
            HalCpuRelax();
        }
    }
}

void DesktopBootLogoShow(void) {
    UINT32 W;
    UINT32 H;
    UINT32 BaseX;
    UINT32 BaseY;
    UINT32 Gap = 24;
    UINT32 Icon = 64;

    HalVideoGetSize(&W, &H);
    if (W < 320 || H < 200) {
        return;
    }

    HalVideoClearScreen(LOGO_BG);

    BaseX = (W - (Icon * 3 + Gap * 2)) / 2;
    BaseY = (H / 2) - 48;
    DrawHammer(BaseX, BaseY);
    DrawFootball(BaseX + Icon + Gap, BaseY);
    DrawSlingshot(BaseX + 2 * (Icon + Gap), BaseY);

    HalVideoDrawStringAt(BaseX + Icon + Gap - 8, BaseY + Icon + 16, "ToyOS",
                         LOGO_FG);
    HalVideoPresentFlush();
    ToyLogBoot("Boot: Logo\n");

    LogoHold();

    HalVideoClearScreen(0x00000000u);
    HalVideoPresentFlush();
    ToyLogBoot("Boot: Logo Cleared\n");
}
