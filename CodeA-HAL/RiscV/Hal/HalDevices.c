/*
 * HalDevices.c — RiscV：注册 / Block·USB 桩 / Wifi·Iwl / Igpu·Hda（PR-S3-haldev-1）
 *
 * Common 只见 Hal*；Input/Net 见 HalDevicesInput.c / HalDevicesNet.c。
 * 板包用 TOY_BOARD_HAS_* 勾选；Duo S 等命令行靶不探 virtio（避免误扫 MMIO）。
 */
#include "Hal.h"
#include "BoardConfig.h"
#include "Driver.h"
#if TOY_BOARD_HAS_BLOCK
#include "VirtioBlock.h"
#endif
#if TOY_BOARD_HAS_FRAMEBUFFER
#include "VirtioInput.h"
#endif
#if TOY_BOARD_HAS_NET
#include "VirtioNet.h"
#endif

void HalDriverRegister(void) {
#if TOY_BOARD_HAS_BLOCK
    VirtioBlockRegister();
#endif
#if TOY_BOARD_HAS_FRAMEBUFFER
    VirtioInputRegister();
#endif
#if TOY_BOARD_HAS_NET
    VirtioNetRegister();
#endif
}

int HalBlockInitialize(void) {
#if TOY_BOARD_HAS_BLOCK
    return VirtioBlockInit();
#else
    return 0;
#endif
}

int HalUsbInitialize(void) {
#if TOY_BOARD_HAS_FRAMEBUFFER
    return VirtioInputInit();
#else
    return 0;
#endif
}

int HalUsbMscInitialize(void) {
    return -1;
}

int HalUsbMscReady(void) {
    return 0;
}

int HalUsbMscScan(void) {
    return -1;
}

int HalUsbMscClaim(void) {
    return -1;
}

int HalUsbMscCapacity(void) {
    return -1;
}

UINT32 HalUsbMscBlockCount(void) {
    return 0;
}

UINT32 HalUsbMscBlockSize(void) {
    return 0;
}

int HalUsbMscMount(void) {
    return -1;
}

int HalUsbMscAutoEnabled(void) {
    return 0;
}

void HalUsbMscAutoSet(int On) {
    (void)On;
}

int HalUsbMscAutoBeforeFs(void) {
    return 1;
}

int HalUsbMscRelease(void) {
    return 0;
}

int HalUsbMscHotPoll(void) {
    return 0;
}

int HalUsbMscHot(void) {
    return 1;
}

int HalUsbUartClaim(void) {
    return -1;
}

int HalUsbUartReady(void) {
    return 0;
}

int HalWifiClaim(void) {
    return 0;
}

int HalWifiReady(void) {
    return 0;
}

int HalIwlClaim(void) {
    return 0;
}

int HalIwlReady(void) {
    return 0;
}

int HalIwlAssociated(void) {
    return 0;
}

int HalIwlBgBusy(void) {
    return 0;
}

void HalIwlBgPump(void) {
}

UINT64 HalIwlLogTsc(void) {
    return 0;
}

void HalIgpuMmioInitialize(void) {
}

void HalHdaMmioInitialize(void) {
}

void HalHdaCodecInitialize(void) {
}

void HalHdaStreamInitialize(void) {
}

int HalAudioProbe(void) {
    return 0;
}

int HalAudioPlayPcm(const void *Samples, UINTN Bytes, UINT32 RateHz,
                    UINT32 Channels, UINT32 Bits) {
    (void)Samples;
    (void)Bytes;
    (void)RateHz;
    (void)Channels;
    (void)Bits;
    return 0;
}

void HalAudioStop(void) {
}

void HalAudioBeep(void) {
}

void HalIgpuGttInitialize(void) {
}

void HalIgpuForcewakeInitialize(void) {
}

void HalIgpuBlitInitialize(void) {
}

void HalIgpuBlitColorTest(void) {
}

void HalIgpuPresentPrepare(void) {
}

void HalIgpuPresentInvalidate(void) {
}

int HalIgpuReady(void) {
    return 0;
}

UINT32 HalIgpuBlitMinPixels(void) {
    return 0;
}

void HalIgpuNotePresentSkipScale(void) {
}

int HalIgpuPresentRect(const UINT32 *Back, UINT32 BackPitchPx, UINT32 FrontPitchPx,
                       UINT32 BackH, UINT32 X0, UINT32 Y0, UINT32 X1, UINT32 Y1) {
    (void)Back;
    (void)BackPitchPx;
    (void)FrontPitchPx;
    (void)BackH;
    (void)X0;
    (void)Y0;
    (void)X1;
    (void)Y1;
    return 0;
}

int HalIgpuCopyRectBack(const UINT32 *Back, UINT32 PitchPx, UINT32 BufH,
                        UINT32 SrcX, UINT32 SrcY, UINT32 DstX, UINT32 DstY,
                        UINT32 W, UINT32 H) {
    (void)Back;
    (void)PitchPx;
    (void)BufH;
    (void)SrcX;
    (void)SrcY;
    (void)DstX;
    (void)DstY;
    (void)W;
    (void)H;
    return 0;
}
