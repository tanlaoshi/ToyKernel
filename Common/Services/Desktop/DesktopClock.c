/*
 * DesktopClock.c — 任务栏时钟 / MSC 热拔节流（PR-S3-desktop-1）
 *
 * 从 Desktop.c 原样搬家；不改语义。
 */
#include "DesktopPrivate.h"
#include "Gui.h" /* GuiCursorHide/Show；GuiDragActive */

void DesktopTickClock(void) {
    static UINT64 LastCheckTick;
    UINT64 Now;
    UINT32 Tps;
    UINT8 Hour = 0;
    UINT8 Minute = 0;
    int Ok;
    int NeedPaint;

    /*
     * 墙钟节流（勿用 Poll 计数）：Gui+Shell 双路径轮询时 Skip=45 几乎每帧
     * 撞 CMOS。UIP 约 1Hz；旧逻辑读失败还 NeedPaint→整条任务栏 Present，
     * 鼠标滑动时体感「约 1 秒顿一次」（全系统顿挫，非仅光标采样）。
     */
    /* 拖窗/改大小时不读 CMOS、不重画任务栏，避免这一拍把鼠标卡住 */
    if (GuiDragActive() || DesktopIconDragActive()) {
        return;
    }

    /* Worker 已加载图标则只刷新；勿在 Gui 路径读盘（与 iwl IoBreath 重入） */
    if (DesktopIconsConsumeNeedRefresh()) {
        RequestRefresh();
    }

    Now = HalCpuTicks(0);
    Tps = HalTicksPerSec();
    if (Tps == 0) {
        Tps = 250;
    }
    if (LastCheckTick != 0 && (Now - LastCheckTick) < (UINT64)(Tps / 2u)) {
        return;
    }
    LastCheckTick = Now;

    /* PR-H-msc-hot：~2Hz 拔出探测（仅 CCS/hub；不 Address） */
    if (HalUsbMscHotPoll() == 1) {
        (void)FileSystemRemountVolumes();
        DebugWrite("desktop: msc hot remount after unplug\n");
    }

    NeedPaint = 0;
    Ok = (HalRtcGetTime(0, 0, 0, &Hour, &Minute, 0) == 0) ? 1 : 0;
    if (Ok) {
        if (!(gClockValid && Hour == gClockHour && Minute == gClockMinute)) {
            NeedPaint = 1;
        }
    }
    /* 瞬时读失败：保留上次 HH:MM，勿刷 --:-- / 勿整栏 Present */
    if (DesktopNetTrayLabelChanged()) {
        NeedPaint = 1;
    }
    if (!NeedPaint) {
        return;
    }
    /*
     * 勿 BeginFront：scale≠100 时逻辑坐标直写物理 GOP → 假任务栏。
     * 后缓冲 + Present；先擦光标再铺栏，避 Alpha/开始钮烙印。
     * 须 ClearClip：Shell 客户区 clip 会让半透底被裁、开始图标仍在。
     */
    {
        UINT32 BarY;
        UINT32 Sw;
        UINT32 Sh;

        TaskbarGeom(&BarY, &Sw, &Sh);
        HalVideoClearClip();
        GuiCursorHide();
        DesktopFillRect(0, BarY, Sw, TASKBAR_H);
        DrawTaskbarRaw();
        if (gMenuOpen) {
            DrawStartMenuRaw();
        }
        if (DesktopNetTrayIsOpen()) {
            DesktopNetTrayDrawPopup();
        }
        GuiCursorShow();
        HalVideoPresent();
    }
}
