/*
 * HdaHdmi.c — Intel HDMI/DP：开 pin、选路、DIP、显示侧 AUD 使能
 * NUC7：Ubuntu DP-2 → pin6/cvt2；无 sense/connlist 时强制该路径。
 */
#include "Hda.h"
#include "Igpu.h"
#include "ToySerialLog.h"

#define AC_VERB_SET_POWER_STATE    0x705u
#define AC_VERB_GET_PIN_SENSE      0xF09u
#define AC_VERB_GET_PARAM          0xF00u
#define AC_PAR_CONNLIST_LEN        0x0Eu
#define AC_VERB_GET_CONNECT_LIST   0xF02u
#define AC_VERB_SET_CONNECT_SEL    0x701u
#define AC_VERB_SET_HDMI_DIP_INDEX 0x730u
#define AC_VERB_SET_HDMI_DIP_DATA  0x731u
#define AC_VERB_SET_HDMI_DIP_XMIT  0x732u
#define AC_DIPXMIT_DISABLE         0x00u
#define AC_DIPXMIT_BEST            0xC0u
#define INTEL_VENDOR_NID           0x08u
#define INTEL_GET_VENDOR_VERB      0xF81u
#define INTEL_SET_VENDOR_VERB      0x781u
#define INTEL_EN_ALL_PIN_CVTS      0x01u
#define INTEL_EN_DP12              0x02u
#define AC_PINSENSE_PRESENCE       (1u << 31)
#define HSW_AUD_PIN_ELD_CP_VLD     0x650C0u
#define HSW_AUD_CFG_A              0x65000u
#define HSW_AUD_CFG_B              0x65100u
#define HSW_AUD_CFG_C              0x65200u
#define AUD_CONFIG_N_VALUE_INDEX   (1u << 29)
#define AUD_CONFIG_N_PROG_ENABLE   (1u << 28)
#define AUD_PIN_BUF_CTL            0x48414u
#define AUD_PIN_BUF_ENABLE         (1u << 31)
#define HSW_AUD_CHICKENBIT         0x65F10u
#define SKL_AUD_CODEC_WAKE         (1u << 15)

static int Verb12(UINT8 Cad, UINT8 Nid, UINT16 V, UINT8 P) {
    return HdaCorbVerb(Cad, Nid, ((UINT32)V << 8) | P, 0);
}

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

int HdaHdmiEnableAllPins(UINT8 Cad, UINT8 Afg) {
    UINT32 V, V2;

    (void)Verb12(Cad, Afg, AC_VERB_SET_POWER_STATE, 0);
    V = 0;
    if (!HdaCorbVerb(Cad, INTEL_VENDOR_NID, (INTEL_GET_VENDOR_VERB << 8) | 0, &V)) {
        return 0;
    }
    V |= INTEL_EN_ALL_PIN_CVTS | INTEL_EN_DP12;
    (void)HdaCorbVerb(Cad, INTEL_VENDOR_NID,
                      (INTEL_SET_VENDOR_VERB << 8) | (V & 0xFFu), 0);
    StallUs(1000);
    V2 = 0;
    (void)HdaCorbVerb(Cad, INTEL_VENDOR_NID, (INTEL_GET_VENDOR_VERB << 8) | 0, &V2);
    ToyLogBoot("Boot: hda intel pins vend=");
    ToyLogBootHex32(V2);
    ToyLogBoot("\n");
    return 1;
}

int HdaHdmiDisplayAudioEnable(void) {
    UINT32 Eld, Cfg, I;
    UINT32 Offs[3];

    if (!IgpuMmioOk()) {
        ToyLogBoot("Boot: hda aud skip (no igpu)\n");
        return 0;
    }
    (void)IgpuForcewakeGet();
    IgpuMmioWrite32(AUD_PIN_BUF_CTL,
                    IgpuMmioRead32(AUD_PIN_BUF_CTL) | AUD_PIN_BUF_ENABLE);
    IgpuMmioWrite32(HSW_AUD_CHICKENBIT,
                    IgpuMmioRead32(HSW_AUD_CHICKENBIT) | SKL_AUD_CODEC_WAKE);
    Eld = IgpuMmioRead32(HSW_AUD_PIN_ELD_CP_VLD);
    /* transcoder A/B/C：AUDIO_OUTPUT_ENABLE */
    Eld |= (1u << 2) | (1u << 6) | (1u << 10);
    IgpuMmioWrite32(HSW_AUD_PIN_ELD_CP_VLD, Eld);
    Eld = IgpuMmioRead32(HSW_AUD_PIN_ELD_CP_VLD);
    ToyLogBoot("Boot: hda aud eld=");
    ToyLogBootHex32(Eld);
    ToyLogBoot("\n");
    /* DP：让 HW 算 Maud/Naud */
    Offs[0] = HSW_AUD_CFG_A;
    Offs[1] = HSW_AUD_CFG_B;
    Offs[2] = HSW_AUD_CFG_C;
    for (I = 0; I < 3u; I++) {
        Cfg = IgpuMmioRead32(Offs[I]);
        if (Cfg == 0xFFFFFFFFu) {
            continue;
        }
        Cfg &= ~AUD_CONFIG_N_PROG_ENABLE;
        Cfg |= AUD_CONFIG_N_VALUE_INDEX;
        IgpuMmioWrite32(Offs[I], Cfg);
    }
    StallUs(2000);
    return 1;
}

static int PinCvt(UINT8 Cad, UINT8 Pin, UINT8 *CvtOut, UINT8 *MuxOut) {
    UINT32 Len, Ent;
    UINT8 Cvt;

    if (!HdaCorbVerb(Cad, Pin, (AC_VERB_GET_PARAM << 8) | AC_PAR_CONNLIST_LEN,
                     &Len) || (Len & 0x7Fu) == 0) {
        return 0;
    }
    if (!HdaCorbVerb(Cad, Pin, (AC_VERB_GET_CONNECT_LIST << 8) | 0, &Ent)) {
        return 0;
    }
    /* short-form：低 7 位为 nid；Intel 偶发读 0 则用固定首 cvt=2 */
    Cvt = (UINT8)(Ent & 0x7Fu);
    if (Cvt == 0) {
        Cvt = 0x02u;
    }
    *CvtOut = Cvt;
    *MuxOut = 0;
    return 1;
}

int HdaHdmiPickPath(UINT8 Cad, UINT8 *PinOut, UINT8 *CvtOut) {
    UINT8 Cand[3] = { 0x06u, 0x05u, 0x07u };
    UINT8 BestPin = 0, BestCvt = 0, BestMux = 0;
    UINT8 FallbackPin = 0, FallbackCvt = 0, FallbackMux = 0;
    UINT32 Sense, Len;
    UINT8 I, Pin, Cvt, Mux;

    for (I = 0; I < 3u; I++) {
        Pin = Cand[I];
        (void)Verb12(Cad, Pin, AC_VERB_SET_POWER_STATE, 0);
        Sense = 0;
        Len = 0;
        (void)HdaCorbVerb(Cad, Pin, (AC_VERB_GET_PIN_SENSE << 8) | 0, &Sense);
        (void)HdaCorbVerb(Cad, Pin, (AC_VERB_GET_PARAM << 8) | AC_PAR_CONNLIST_LEN,
                          &Len);
        ToyLogBoot("Boot: hda pin=");
        ToyLogBootHex32((UINT32)Pin);
        ToyLogBoot(" sense=");
        ToyLogBootHex32(Sense);
        ToyLogBoot(" cl=");
        ToyLogBootHex32(Len);
        ToyLogBoot("\n");
        if (!PinCvt(Cad, Pin, &Cvt, &Mux)) {
            continue;
        }
        if (FallbackPin == 0) {
            FallbackPin = Pin;
            FallbackCvt = Cvt;
            FallbackMux = Mux;
        }
        if (Sense & AC_PINSENSE_PRESENCE) {
            BestPin = Pin;
            BestCvt = Cvt;
            BestMux = Mux;
            break;
        }
    }
    if (BestPin == 0) {
        BestPin = FallbackPin;
        BestCvt = FallbackCvt;
        BestMux = FallbackMux;
    }
    /* 无 connlist/sense：本课 DP-2 = PORT C = pin6 / cvt2 */
    if (BestPin == 0) {
        BestPin = 0x06u;
        BestCvt = 0x02u;
        BestMux = 0;
        ToyLogBoot("Boot: hda pick force pin=6 cvt=2\n");
    }
    (void)Verb12(Cad, BestPin, AC_VERB_SET_CONNECT_SEL, BestMux);
    *PinOut = BestPin;
    *CvtOut = BestCvt;
    ToyLogBoot("Boot: hda pick pin=");
    ToyLogBootHex32((UINT32)BestPin);
    ToyLogBoot(" cvt=");
    ToyLogBootHex32((UINT32)BestCvt);
    ToyLogBoot("\n");
    return 1;
}

int HdaHdmiInfoframe(UINT8 Cad, UINT8 Pin) {
    UINT8 Ai[32];
    UINTN I;

    for (I = 0; I < sizeof(Ai); I++) {
        Ai[I] = 0;
    }
    /* DisplayPort audio infoframe（本课 DELL U2415） */
    Ai[0] = 0x84u;
    Ai[1] = 0x1Bu;
    Ai[2] = (UINT8)(0x11u << 2);
    Ai[3] = 0x01u;

    (void)Verb12(Cad, Pin, AC_VERB_SET_HDMI_DIP_INDEX, 0);
    (void)Verb12(Cad, Pin, AC_VERB_SET_HDMI_DIP_XMIT, AC_DIPXMIT_DISABLE);
    (void)Verb12(Cad, Pin, AC_VERB_SET_HDMI_DIP_INDEX, 0);
    for (I = 0; I < sizeof(Ai); I++) {
        (void)Verb12(Cad, Pin, AC_VERB_SET_HDMI_DIP_DATA, Ai[I]);
    }
    (void)Verb12(Cad, Pin, AC_VERB_SET_HDMI_DIP_INDEX, 0);
    (void)Verb12(Cad, Pin, AC_VERB_SET_HDMI_DIP_XMIT, AC_DIPXMIT_BEST);
    ToyLogBoot("Boot: hda infoframe dp\n");
    return 1;
}
