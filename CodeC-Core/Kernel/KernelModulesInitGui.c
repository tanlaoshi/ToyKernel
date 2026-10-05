/*
 * KernelModulesInitGui.c — InitializeGui（PR-K-seq-4）
 *
 * 人话：读主题/字体/语言后开桌面合成。真机路径上穿插抽空输入，避免键鼠假死。
 */
#include "KernelModulesPrivate.h"
#include "Hal.h"
#include "Gui.h"
#include "Font.h"
#include "Theme.h"
#include "Db.h"
#include "Locale.h"

static void KernelModulesInitGuiPollInput(void) {
    if (!HalCpuIsHypervisor()) {
        HalInputPoll();
    }
}

static void KernelModulesInitGuiDrainMouse(void) {
    HAL_MOUSE_REPORT Mouse;

    if (HalCpuIsHypervisor()) {
        return;
    }
    HalInputPoll();
    while (HalMouseDequeue(&Mouse)) {
    }
}

int InitializeGui(void) {
    KernelModulesInitGuiPollInput();
    (void)DbInitialize();
    (void)FontLoadAssets();
    (void)FontTtfLoad();
    (void)FontTtfInit();
    KernelModulesInitGuiPollInput();
    (void)ThemeLoad();
    LocaleInitialize();
    KernelModulesInitGuiPollInput();
    if (ThemeUiScale() != 100) {
        (void)HalVideoSetUiScale(ThemeUiScale());
    }
    GuiInitialize();
    KernelModulesInitGuiDrainMouse();
    return 0;
}
