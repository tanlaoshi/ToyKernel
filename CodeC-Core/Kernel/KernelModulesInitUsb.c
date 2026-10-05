/*
 * KernelModulesInitUsb.c — InitializeUsb（PR-K-seq-4）
 *
 * 人话：探 USB/PS2 输入。没有键盘也必须继续进桌面。
 * USB-UART 仍在 FS 认盘后再 claim，勿在这里扫口。
 */
#include "KernelModulesPrivate.h"
#include "Hal.h"
#include "DriverInput.h"
#include "ToySerialLog.h"

static void KernelModulesInitUsbHandoffMouse(void) {
    UINT32 CenterX = 512;
    UINT32 CenterY = 384;
    UINT32 ScreenW = 0;
    UINT32 ScreenH = 0;

    HalVideoGetSize(&ScreenW, &ScreenH);
    if (ScreenW > 0) {
        CenterX = ScreenW / 2;
    }
    if (ScreenH > 0) {
        CenterY = ScreenH / 2;
    }
    HalInputMouseHandoffDesktop(CenterX, CenterY);
}

static void KernelModulesInitUsbDrainHid(void) {
    int Index;
    HAL_KEYBOARD_REPORT Keys;
    HAL_MOUSE_REPORT Mouse;

    for (Index = 0; Index < 8; Index++) {
        HalInputPoll();
    }
    while (HalKeyboardDequeue(&Keys)) {
    }
    while (HalMouseDequeue(&Mouse)) {
    }
}

int InitializeUsb(void) {
    ToyLogBoot("Boot: Input Probe (USB Then PS/2)\n");
    (void)HalUsbInitialize();
    if (ToyDriverInputReady()) {
        ToyLogBoot("Boot: Input Backend Ready\n");
    } else {
        ToyLogBoot("Boot: Input NONE (Continue)\n");
    }
    if (!HalCpuIsHypervisor()) {
        HalInputArmIrq();
        KernelModulesInitUsbHandoffMouse();
        KernelModulesInitUsbDrainHid();
    }
    return 0;
}
