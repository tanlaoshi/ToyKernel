/*
 * HalDevices.c — x86：注册 / USB·MSC / Wifi·Iwl / Igpu·Hda（PR-S3-haldev-1）
 *
 * Common 只见 Hal*；Input/Net 门面见 HalDevicesInput.c / HalDevicesNet.c。
 */
#include "Hal.h"
#include "DriverInput.h"
#include "DriverNet.h"
#include "InputXhci.h"
#include "InputPs2.h"
#include "InputEhci.h"
#include "InputUhci.h"
#include "UsbMsc.h"
#include "Net.h"
#include "Ehci.h"
#include "XHCI.h"
#include "Wifi.h"
#include "Iwl.h"
#include "Igpu.h"
#include "Hda.h"
#include "Driver.h"

#ifndef TOY_DEMO_DRIVER
#define TOY_DEMO_DRIVER 1
#endif

/* BlockAta / BlockAhci（H1）/ BlockNvme（H5） */
void AtaDriverRegister(void);
void AhciDriverRegister(void);
void NvmeDriverRegister(void);
void MscDriverRegister(void); /* PR-H-msc：空壳注册；认盘在后续 PR */
void E1000DriverRegister(void);
void AlxDriverRegister(void); /* PR-N-alx-2：AR8161 L2；无卡/无链路不挡 */
void RtlDriverRegister(void); /* PR-N-rtl-1：r8169 Probe/MAC；无卡不挡 */
void WifiDriverRegister(void); /* USB 8188EU 骨架；无棒不挡 */
void IwlDriverRegister(void);  /* PR-N-wifi-1：iwl8265；FS 后 Claim */
void IgpuDriverRegister(void); /* PR-G-igpu-0：Intel display 认卡 */
void HdaDriverRegister(void);  /* PR-G-audio-0：Intel HDA 认卡 */
void DemoDriverRegister(void); /* PR-D-tpl-2 */

void HalDriverRegister(void) {
    /* 后注册者在 HalBlockInit 再 Probe 时可覆盖后端：NVMe > AHCI > ATA */
    AhciDriverRegister();
    AtaDriverRegister();
    NvmeDriverRegister();
    MscDriverRegister(); /* PR-H-msc-1：Bind 不改 Block 后端 */
    InputXhciRegister(); /* 先 USB HID（xHCI） */
    InputEhciRegister(); /* PR-H-ehci-1：EHCI CCS；HID→ehci-2 */
    InputUhciRegister(); /* PR-H-uhci-1：UHCI CCS 骨架 */
    InputPs2Register();  /* 后 PS/2：PR-H-input-mux 与 USB 可并存 */
    NetDriverRegister();
    E1000DriverRegister(); /* PR-H4：无卡 Probe 失败；有卡时可覆盖 virtio */
    AlxDriverRegister();   /* PR-N-alx-2：Bind → NetAttachNic */
    RtlDriverRegister();   /* PR-N-rtl-1：lsdev=r8169；rtl-2 再挂 L2 */
    WifiDriverRegister();  /* USB 8188EU 软失败骨架 */
    IwlDriverRegister();   /* PR-N-wifi-1：NUC 8265；HalIwlClaim 后再绑 */
    IgpuDriverRegister();  /* PR-G-igpu-0：NUC UHD；QEMU 无卡软退 */
    HdaDriverRegister();   /* PR-G-audio-0：NUC/QEMU HDA；无卡软退 */
#if TOY_DEMO_DRIVER
    DemoDriverRegister(); /* PR-D-tpl-2：课堂 Demo；-DTOY_DEMO_DRIVER=0 可关 */
#endif
}

int HalUsbInit(void) {
    return InputXhciInit();
}

int HalUsbMscInit(void) {
    return UsbMscInit();
}

int HalUsbMscReady(void) {
    return UsbMscReady();
}

int HalUsbMscScan(void) {
    return UsbMscScan();
}

int HalUsbMscClaim(void) {
    return UsbMscClaim();
}

int HalUsbMscCapacity(void) {
    return UsbMscCapacity();
}

UINT32 HalUsbMscBlockCount(void) {
    return UsbMscBlockCount();
}

UINT32 HalUsbMscBlockSize(void) {
    return UsbMscBlockSize();
}

int HalUsbMscMount(void) {
    return UsbMscMount();
}

int HalUsbMscAutoEnabled(void) {
    return UsbMscAutoEnabled();
}

void HalUsbMscAutoSet(int On) {
    UsbMscAutoSet(On);
}

int HalUsbMscAutoBeforeFs(void) {
    return UsbMscAutoBeforeFs();
}

int HalUsbMscRelease(void) {
    return UsbMscRelease();
}

int HalUsbMscHotPoll(void) {
    return UsbMscHotPoll();
}

int HalUsbMscHot(void) {
    return UsbMscHot();
}

int HalUsbUartClaim(void) {
    int Rc;

    /* FTDI：先 xHCI，再 EHCI（N56VZ 棒在 RMH）；已认则 CDC 互斥跳过 */
    Rc = XhciFtdiClaim();
    if (Rc == 1) {
        return 1;
    }
    if (EhciReady()) {
        Rc = EhciFtdiClaim();
        if (Rc == 1) {
            return 1;
        }
    }
    return XhciCdcClaim();
}

int HalUsbUartReady(void) {
    return (XhciFtdiReady() || EhciFtdiReady() || XhciCdcReady()) ? 1 : 0;
}

/* PR-N-wifi-1：USB 8188EU；先 xHCI 再 EHCI；无棒 0 */
int HalWifiClaim(void) {
    return WifiSetup() ? 1 : 0;
}

int HalWifiReady(void) {
    return WifiReady();
}

/* PR-N-wifi-1 / PR-BOOT-fast-3：放行后 PCI+BAR；FW/关联仍走 Worker BgPump；无卡 0 */
int HalIwlClaim(void) {
    IwlAllowBootClaim();
    return IwlSetup() ? 1 : 0;
}

int HalIwlReady(void) {
    return IwlReady();
}

int HalIwlBgBusy(void) {
    return IwlNetBgBusy();
}

void HalIwlBgPump(void) {
    IwlNetBgPump();
}

void HalIgpuMmioInit(void) {
    (void)IgpuMmioInit();
}

void HalHdaMmioInit(void) {
    (void)HdaMmioInit();
}

void HalHdaCodecInit(void) {
    (void)HdaCodecInit();
}

void HalHdaStreamInit(void) {
    (void)HdaStreamInit();
}

int HalAudioProbe(void) {
    return HdaAudioProbe();
}

int HalAudioPlayPcm(const void *Samples, UINTN Bytes, UINT32 RateHz,
                    UINT32 Channels, UINT32 Bits) {
    return HdaAudioPlayPcm(Samples, Bytes, RateHz, Channels, Bits);
}

void HalAudioStop(void) {
    HdaAudioStop();
}

void HalAudioBeep(void) {
    (void)HdaAudioPlayPcm(0, 0, 48000u, 2u, 16u);
}

void HalIgpuGttInit(void) {
    (void)IgpuGttInit();
}

void HalIgpuForcewakeInit(void) {
    (void)IgpuForcewakeInit();
}

void HalIgpuBlitInit(void) {
    (void)IgpuBlitInit();
}

void HalIgpuBlitColorTest(void) {
    (void)IgpuBlitColorTest();
}

void HalIgpuPresentPrepare(void) {
    IgpuPresentPrepare();
}

void HalIgpuPresentInvalidate(void) {
    IgpuPresentInvalidate();
}

int HalIgpuReady(void) {
    /* 仅 SRC_COPY 探针通过才走 GPU Present；否则 CPU memcpy，避免黑屏只剩光标 */
    return IgpuReady() && IgpuPresentCopyOk();
}

UINT32 HalIgpuBlitMinPixels(void) {
    return IgpuBlitMinPixels();
}

void HalIgpuNotePresentSkipScale(void) {
    IgpuNotePresentSkipScale();
}

int HalIgpuPresentRect(const UINT32 *Back, UINT32 BackPitchPx, UINT32 FrontPitchPx,
                       UINT32 BackH, UINT32 X0, UINT32 Y0, UINT32 X1, UINT32 Y1) {
    return IgpuPresentRect(Back, BackPitchPx, FrontPitchPx, BackH, X0, Y0, X1, Y1);
}

int HalIgpuCopyRectBack(const UINT32 *Back, UINT32 PitchPx, UINT32 BufH,
                        UINT32 SrcX, UINT32 SrcY, UINT32 DstX, UINT32 DstY,
                        UINT32 W, UINT32 H) {
    return IgpuCopyRectBack(Back, PitchPx, BufH, SrcX, SrcY, DstX, DstY, W, H);
}
