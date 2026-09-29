/*
 * HdaStream.c — PR-G-audio-3：Stream+BDL+PCM（HDMI/DP→显示器耳机孔）
 * 48k/16/2；PCH Tag=SDCTL[23:20]；选路/InfoFrame→HdaHdmi。
 */
#include "Hda.h"
#include "PhysicalMemory.h"
#include "VirtualMemory.h"
#include "ToySerialLog.h"
#include "Hal.h"
#ifndef PTE_PWT
#define PTE_PWT HAL_PAGE_PWT
#define PTE_PCD HAL_PAGE_PCD
#endif

#define HDA_REG_GCAP               0x00u
#define AC_VERB_SET_STREAM_FORMAT  0x200u
#define AC_VERB_SET_AMP_GAIN_MUTE  0x300u
#define AC_VERB_SET_POWER_STATE    0x705u
#define AC_VERB_SET_CHANNEL_STREAMID 0x706u
#define AC_VERB_SET_PIN_WIDGET_CONTROL 0x707u
#define AC_VERB_SET_DIGI_CONVERT_1 0x70Du
#define AC_VERB_SET_EAPD_BTLENABLE 0x70Cu
#define AC_VERB_SET_CVT_CHAN_COUNT 0x72Du
#define AC_VERB_SET_HDMI_CHAN_SLOT 0x734u
#define HDA_FMT_48K_16_2           0x0011u
#define HDA_STREAM_TAG             1u
#define HDA_SD_SRST                0x01u
#define HDA_SD_RUN                 0x02u
#define HDA_SD_IOCE                0x04u
#define HDA_SD_STS_BCIS            0x04u
#define HDA_SD_STS_FIFORDY         0x20u
#define HDA_SD_TAG_SHIFT           20u
#define HDA_PINCTL_OUT_EN          0x40u
#define HDA_PCM_PAGES              2u
#define HDA_BDL_ENTRIES            2u
#define HDA_BEEP_REPEATS           8u

static int gHdaStreamOk;
static UINT32 gSdBase;

static void StallUs(UINT32 Us) {
    UINT32 Lo, Hi;
    UINT64 T0, Need, Now;
    if (Us == 0) {
        return;
    }
    __asm__ volatile ("rdtsc" : "=a"(Lo), "=d"(Hi));
    T0 = ((UINT64)Hi << 32) | Lo;
    Need = (UINT64)Us * 3000ULL;
    do {
        __asm__ volatile ("rdtsc" : "=a"(Lo), "=d"(Hi));
        Now = ((UINT64)Hi << 32) | Lo;
        __asm__ volatile ("pause");
    } while (Now - T0 < Need);
}

static void Zero(void *P, UINTN N) {
    UINT8 *B = (UINT8 *)P;
    while (N--) {
        *B++ = 0;
    }
}

static int Verb12(UINT8 Cad, UINT8 Nid, UINT16 V, UINT8 P) {
    return HdaCorbVerb(Cad, Nid, ((UINT32)V << 8) | P, 0);
}

static int Verb4(UINT8 Cad, UINT8 Nid, UINT16 V, UINT16 P) {
    return HdaCorbVerb(Cad, Nid, ((UINT32)V << 8) | P, 0);
}

static int CodecPathSetup(UINT8 Cad, UINT8 Afg, UINT8 Pin, UINT8 Cvt) {
    (void)Verb12(Cad, Afg, AC_VERB_SET_POWER_STATE, 0);
    (void)Verb12(Cad, Cvt, AC_VERB_SET_POWER_STATE, 0);
    (void)Verb12(Cad, Pin, AC_VERB_SET_POWER_STATE, 0);
    (void)Verb12(Cad, Cvt, AC_VERB_SET_CVT_CHAN_COUNT, 1);
    (void)Verb12(Cad, Cvt, AC_VERB_SET_HDMI_CHAN_SLOT, 0x00u);
    (void)Verb12(Cad, Cvt, AC_VERB_SET_HDMI_CHAN_SLOT, 0x11u);
    (void)Verb4(Cad, Cvt, AC_VERB_SET_STREAM_FORMAT, HDA_FMT_48K_16_2);
    (void)Verb12(Cad, Cvt, AC_VERB_SET_CHANNEL_STREAMID,
                 (UINT8)((HDA_STREAM_TAG << 4) | 0));
    (void)Verb12(Cad, Pin, AC_VERB_SET_PIN_WIDGET_CONTROL, HDA_PINCTL_OUT_EN);
    (void)Verb12(Cad, Cvt, AC_VERB_SET_DIGI_CONVERT_1, 1);
    (void)Verb12(Cad, Pin, AC_VERB_SET_EAPD_BTLENABLE, 2);
    (void)Verb4(Cad, Cvt, AC_VERB_SET_AMP_GAIN_MUTE, 0xB000u);
    (void)Verb4(Cad, Pin, AC_VERB_SET_AMP_GAIN_MUTE, 0xB000u);
    (void)HdaHdmiInfoframe(Cad, Pin);
    StallUs(2000);
    return 1;
}

static UINT32 SdOff(UINT32 Rel) {
    return gSdBase + Rel;
}

static int StreamReset(void) {
    UINT32 I;
    HdaMmioWrite8(SdOff(0x00), HDA_SD_SRST);
    for (I = 0; I < 1000u; I++) {
        if (HdaMmioRead8(SdOff(0x00)) & HDA_SD_SRST) {
            break;
        }
        StallUs(10);
    }
    HdaMmioWrite8(SdOff(0x00), 0);
    for (I = 0; I < 1000u; I++) {
        if ((HdaMmioRead8(SdOff(0x00)) & HDA_SD_SRST) == 0) {
            return 1;
        }
        StallUs(10);
    }
    return 0;
}

static void FillSquare(INT16 *Pcm, UINTN Frames) {
    UINTN I;
    for (I = 0; I < Frames; I++) {
        INT16 S = ((I / 32u) & 1u) ? (INT16)28000 : (INT16)-28000;
        Pcm[I * 2u] = S;
        Pcm[I * 2u + 1u] = S;
    }
}

static int PlayOnce(UINT8 *Dma, UINT64 Phys) {
    UINT32 *Bdl = (UINT32 *)(UINTN)Dma;
    INT16 *Pcm = (INT16 *)(UINTN)(Dma + PAGE_SIZE);
    UINTN PcmBytes = HDA_PCM_PAGES * PAGE_SIZE;
    UINTN Frames = PcmBytes / 4u;
    UINT32 Half = (UINT32)(PcmBytes / 2u);
    UINT32 I, Ctl, Lpib;
    UINT8 Sts;

    FillSquare(Pcm, Frames);
    Zero(Bdl, PAGE_SIZE);
    Bdl[0] = (UINT32)(Phys + PAGE_SIZE);
    Bdl[1] = 0;
    Bdl[2] = Half;
    Bdl[3] = 0;
    Bdl[4] = (UINT32)(Phys + PAGE_SIZE + Half);
    Bdl[5] = 0;
    Bdl[6] = Half;
    Bdl[7] = 1u;
    if (!StreamReset()) {
        ToyLogBoot("Boot: hda stream reset fail\n");
        return 0;
    }
    HdaMmioWrite32(SdOff(0x18), (UINT32)Phys);
    HdaMmioWrite32(SdOff(0x1C), 0);
    HdaMmioWrite32(SdOff(0x08), (UINT32)PcmBytes);
    HdaMmioWrite16(SdOff(0x0C), (UINT16)(HDA_BDL_ENTRIES - 1u));
    HdaMmioWrite16(SdOff(0x12), HDA_FMT_48K_16_2);
    HdaMmioWrite8(SdOff(0x03), 0x1Cu);
    Ctl = (UINT32)HDA_STREAM_TAG << HDA_SD_TAG_SHIFT;
    HdaMmioWrite32(SdOff(0x00), Ctl);
    for (I = 0; I < 1000u; I++) {
        if (HdaMmioRead8(SdOff(0x03)) & HDA_SD_STS_FIFORDY) {
            break;
        }
        StallUs(10);
    }
    HdaMmioWrite32(SdOff(0x00), Ctl | HDA_SD_IOCE | HDA_SD_RUN);
    for (I = 0; I < 5000u; I++) {
        Sts = HdaMmioRead8(SdOff(0x03));
        Lpib = HdaMmioRead32(SdOff(0x04));
        if ((Sts & HDA_SD_STS_BCIS) || Lpib >= (UINT32)PcmBytes - 64u) {
            if (Sts & HDA_SD_STS_BCIS) {
                HdaMmioWrite8(SdOff(0x03), HDA_SD_STS_BCIS);
            }
            HdaMmioWrite32(SdOff(0x00), 0);
            return 1;
        }
        StallUs(100);
    }
    Lpib = HdaMmioRead32(SdOff(0x04));
    HdaMmioWrite32(SdOff(0x00), 0);
    ToyLogBoot("Boot: hda stream timeout sts=");
    ToyLogBootHex32((UINT32)HdaMmioRead8(SdOff(0x03)));
    ToyLogBoot(" lpib=");
    ToyLogBootHex32(Lpib);
    ToyLogBoot("\n");
    return 0;
}

int HdaStreamOk(void) {
    return gHdaStreamOk;
}

int HdaStreamInit(void) {
    UINT16 Gcap;
    UINT8 Iss, Cad, Afg, Pin, Cvt;
    UINT8 *Dma;
    UINT64 Phys;
    UINT32 R;

    if (gHdaStreamOk) {
        return 1;
    }
    if (!HdaCodecOk()) {
        return 0;
    }
    Cad = HdaCodecAddr();
    Afg = HdaCodecAfg();
    if (Cad == 0xFFu || Afg == 0) {
        ToyLogBoot("Boot: hda stream no path\n");
        return 0;
    }
    if (!HdaHdmiPickPath(Cad, &Pin, &Cvt)) {
        ToyLogBoot("Boot: hda stream no pin/cvt\n");
        return 0;
    }
    (void)HdaHdmiDisplayAudioEnable();
    Gcap = HdaMmioRead16(HDA_REG_GCAP);
    Iss = (UINT8)((Gcap >> 8) & 0x0Fu);
    gSdBase = 0x80u + (UINT32)Iss * 0x20u;
    if (!CodecPathSetup(Cad, Afg, Pin, Cvt)) {
        return 0;
    }
    Dma = (UINT8 *)PhysicalMemoryAllocatePages(1u + HDA_PCM_PAGES);
    if (!Dma) {
        ToyLogBoot("Boot: hda stream dma fail\n");
        return 0;
    }
    Phys = (UINT64)(UINTN)Dma;
    if (Phys > 0xFFFFFFFFu) {
        ToyLogBoot("Boot: hda stream dma >4G\n");
        return 0;
    }
    Zero(Dma, (1u + HDA_PCM_PAGES) * PAGE_SIZE);
    if (VirtualMemoryMapRange(Phys, Phys, (1u + HDA_PCM_PAGES) * PAGE_SIZE,
                              PTE_PRESENT | PTE_WRITABLE | PTE_PWT | PTE_PCD) != 0) {
        ToyLogBoot("Boot: hda stream map fail\n");
        return 0;
    }
    for (R = 0; R < HDA_BEEP_REPEATS; R++) {
        if (!PlayOnce(Dma, Phys)) {
            (void)Verb12(Cad, Pin, AC_VERB_SET_PIN_WIDGET_CONTROL, 0);
            (void)Verb12(Cad, Cvt, AC_VERB_SET_CHANNEL_STREAMID, 0);
            return 0;
        }
    }
    (void)Verb12(Cad, Cvt, AC_VERB_SET_CHANNEL_STREAMID, 0);
    gHdaStreamOk = 1;
    ToyLogBoot("Boot: hda stream ok tag=");
    ToyLogBootHex32(HDA_STREAM_TAG);
    ToyLogBoot(" sd=");
    ToyLogBootHex32(gSdBase);
    ToyLogBoot("\n");
    return 1;
}
