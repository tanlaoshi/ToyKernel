/*
 * HdaCodec.c — PR-G-audio-2：枚举 codec/输出 pin。本课 NUC 无耳机孔=HDMI 主路径。
 */
#include "Hda.h"
#include "ToySerialLog.h"

#define HDA_REG_STATESTS           0x0Eu
#define AC_VERB_GET_PARAM          0xF00u
#define AC_PAR_VENDOR_ID           0x00u
#define AC_PAR_NODE_COUNT          0x04u
#define AC_PAR_FUNCTION_TYPE       0x05u
#define AC_PAR_AUDIO_WIDGET_CAP    0x09u
#define AC_PAR_PIN_CAP             0x0Cu
#define AC_VERB_GET_CONFIG_DEFAULT 0xF1Cu

#define AC_WID_PIN                 0x4u
#define AC_PINCAP_OUT              (1u << 4)
#define AC_JACK_LINE_OUT           0x0u
#define AC_JACK_SPEAKER            0x1u
#define AC_JACK_HP_OUT             0x2u
#define AC_JACK_CONN_NONE          0x1u

typedef struct {
    UINT8 Cad;
    UINT8 Afg;
    UINT8 AnalogPins;
    UINT8 DigPins;
    UINT8 FirstOutNid;
    UINT32 Vendor;
    UINT32 FirstCfg;
    int Ok;
} HDA_CODEC_PROBE;

static int gHdaCodecOk;
static UINT8 gCodecCad = 0xFFu;
static UINT8 gCodecAfg;
static UINT8 gOutPins;
static UINT8 gFirstOutNid;
static UINT32 gVendor;

int HdaCodecOk(void) {
    return gHdaCodecOk;
}

UINT8 HdaCodecAddr(void) {
    return gCodecCad;
}

UINT8 HdaCodecAfg(void) {
    return gCodecAfg;
}

UINT8 HdaCodecOutPins(void) {
    return gOutPins;
}

UINT8 HdaCodecFirstOutNid(void) {
    return gFirstOutNid;
}

static int GetParam(UINT8 Cad, UINT8 Nid, UINT8 Param, UINT32 *Out) {
    return HdaCorbVerb(Cad, Nid, (AC_VERB_GET_PARAM << 8) | Param, Out);
}

/* 1=模拟；0=HDMI/数字。认 PinCap OUT；Device 异常仍收。 */
static int ClassifyOutPin(UINT32 PinCap, UINT32 Cfg, int *IsAnalog) {
    UINT8 Conn = (UINT8)((Cfg >> 30) & 3u);
    UINT8 Dev = (UINT8)((Cfg >> 20) & 0xFu);
    int Analog = 0;

    if ((PinCap & AC_PINCAP_OUT) == 0 || Conn == AC_JACK_CONN_NONE) {
        return 0;
    }
    if (Dev == AC_JACK_LINE_OUT || Dev == AC_JACK_SPEAKER ||
        Dev == AC_JACK_HP_OUT) {
        Analog = 1;
    }
    if (IsAnalog) {
        *IsAnalog = Analog;
    }
    return 1;
}

static int ScanAfgWidgets(UINT8 Cad, UINT8 Afg, HDA_CODEC_PROBE *Out) {
    UINT32 Sub;
    UINT32 Cap;
    UINT32 PinCap;
    UINT32 Cfg;
    UINT8 Start;
    UINT8 Count;
    UINT8 I;
    UINT8 FirstAnalogNid = 0;
    UINT8 FirstDigNid = 0;
    UINT32 FirstAnalogCfg = 0;
    UINT32 FirstDigCfg = 0;

    if (!GetParam(Cad, Afg, AC_PAR_NODE_COUNT, &Sub)) {
        return 0;
    }
    Start = (UINT8)((Sub >> 16) & 0xFFu);
    Count = (UINT8)(Sub & 0xFFu);
    if (Count == 0 || Count > 0x7Fu) {
        return 0;
    }
    Out->AnalogPins = 0;
    Out->DigPins = 0;
    for (I = 0; I < Count; I++) {
        UINT8 Nid = (UINT8)(Start + I);
        UINT8 Type;
        int Analog = 0;

        if (!GetParam(Cad, Nid, AC_PAR_AUDIO_WIDGET_CAP, &Cap)) {
            continue;
        }
        Type = (UINT8)((Cap >> 20) & 0xFu);
        if (Type != AC_WID_PIN) {
            continue;
        }
        if (!GetParam(Cad, Nid, AC_PAR_PIN_CAP, &PinCap)) {
            continue;
        }
        if (!HdaCorbVerb(Cad, Nid, AC_VERB_GET_CONFIG_DEFAULT << 8, &Cfg)) {
            continue;
        }
        if (!ClassifyOutPin(PinCap, Cfg, &Analog)) {
            continue;
        }
        if (Analog) {
            Out->AnalogPins++;
            if (FirstAnalogNid == 0) {
                FirstAnalogNid = Nid;
                FirstAnalogCfg = Cfg;
            }
        } else {
            Out->DigPins++;
            if (FirstDigNid == 0) {
                FirstDigNid = Nid;
                FirstDigCfg = Cfg;
            }
        }
    }
    if (FirstAnalogNid != 0) {
        Out->FirstOutNid = FirstAnalogNid;
        Out->FirstCfg = FirstAnalogCfg;
    } else {
        Out->FirstOutNid = FirstDigNid;
        Out->FirstCfg = FirstDigCfg;
    }
    return (Out->AnalogPins + Out->DigPins) > 0;
}

static int ProbeCad(UINT8 Cad, HDA_CODEC_PROBE *Out) {
    UINT32 Vend;
    UINT32 Sub;
    UINT32 Ft;
    UINT8 Start;
    UINT8 Count;
    UINT8 I;
    UINT8 Afg = 0;

    Out->Ok = 0;
    Out->Cad = Cad;
    if (!GetParam(Cad, 0, AC_PAR_VENDOR_ID, &Vend) || Vend == 0 ||
        Vend == 0xFFFFFFFFu) {
        return 0;
    }
    if (!GetParam(Cad, 0, AC_PAR_NODE_COUNT, &Sub)) {
        return 0;
    }
    Start = (UINT8)((Sub >> 16) & 0xFFu);
    Count = (UINT8)(Sub & 0xFFu);
    if (Count == 0 || Count > 0x1Fu) {
        return 0;
    }
    for (I = 0; I < Count; I++) {
        UINT8 Nid = (UINT8)(Start + I);
        if (!GetParam(Cad, Nid, AC_PAR_FUNCTION_TYPE, &Ft)) {
            continue;
        }
        if ((Ft & 0xFFu) == 0x01u) {
            Afg = Nid;
            break;
        }
    }
    if (Afg == 0) {
        return 0;
    }
    Out->Vendor = Vend;
    Out->Afg = Afg;
    ToyLogBoot("Boot: hda codec a=");
    ToyLogBootHex32((UINT32)Cad);
    ToyLogBoot(" vend=");
    ToyLogBootHex32(Vend);
    ToyLogBoot(" afg=");
    ToyLogBootHex32((UINT32)Afg);
    ToyLogBoot("\n");
    if ((Vend >> 16) == 0x8086u && ((Vend >> 8) & 0xFFu) == 0x28u) {
        (void)HdaHdmiEnableAllPins(Cad, Afg);
    }
    if (!ScanAfgWidgets(Cad, Afg, Out)) {
        ToyLogBoot("Boot: hda codec no out pin\n");
        return 0;
    }
    Out->Ok = 1;
    return 1;
}

static int ScoreProbe(const HDA_CODEC_PROBE *P) {
    int S;
    UINT16 Vid;

    if (!P || !P->Ok) {
        return -1;
    }
    Vid = (UINT16)(P->Vendor >> 16);
    S = (int)P->AnalogPins * 10 + (int)P->DigPins * 10;
    /* 本课 NUC 无耳机孔：Intel HDMI (8086:28xx) 即主路径 */
    if (Vid == 0x8086u && P->DigPins > 0) {
        S += 1000;
    } else if (Vid == 0x10ECu && P->AnalogPins > 0) {
        S += 500;
    } else if (P->AnalogPins > 0) {
        S += 100;
    }
    return S;
}

int HdaCodecInit(void) {
    UINT16 Sts;
    UINT8 Cad;
    HDA_CODEC_PROBE Best;
    HDA_CODEC_PROBE Cur;
    int BestScore;

    if (gHdaCodecOk) {
        return 1;
    }
    if (!HdaCorbInit()) {
        return 0;
    }

    Sts = HdaMmioRead16(HDA_REG_STATESTS) & 0x7FFFu;
    ToyLogBoot("Boot: hda enum sts=");
    ToyLogBootHex32((UINT32)Sts);
    ToyLogBoot("\n");
    if (Sts == 0) {
        ToyLogBoot("Boot: hda codec none (STATESTS=0)\n");
        return 0;
    }
    HdaMmioWrite16(HDA_REG_STATESTS, Sts);

    BestScore = -1;
    Best.Ok = 0;
    Best.Cad = 0xFFu;
    Best.AnalogPins = 0;
    Best.DigPins = 0;
    Best.FirstOutNid = 0;
    Best.FirstCfg = 0;
    Best.Afg = 0;
    Best.Vendor = 0;
    for (Cad = 0; Cad < 15u; Cad++) {
        int Sc;

        if (((Sts >> Cad) & 1u) == 0) {
            continue;
        }
        if (!ProbeCad(Cad, &Cur)) {
            continue;
        }
        Sc = ScoreProbe(&Cur);
        if (Sc > BestScore) {
            BestScore = Sc;
            Best = Cur;
        }
    }
    if (!Best.Ok || BestScore < 0) {
        ToyLogBoot("Boot: hda codec enum fail\n");
        return 0;
    }

    gCodecCad = Best.Cad;
    gCodecAfg = Best.Afg;
    gVendor = Best.Vendor;
    gOutPins = (UINT8)(Best.AnalogPins + Best.DigPins);
    gFirstOutNid = Best.FirstOutNid;
    gHdaCodecOk = 1;
    ToyLogBoot("Boot: hda pin nid=");
    ToyLogBootHex32((UINT32)Best.FirstOutNid);
    ToyLogBoot(" cfg=");
    ToyLogBootHex32(Best.FirstCfg);
    ToyLogBoot("\n");
    ToyLogBoot("Boot: hda codec pins=");
    ToyLogBootHex32((UINT32)gOutPins);
    ToyLogBoot(" outnid=");
    ToyLogBootHex32((UINT32)gFirstOutNid);
    ToyLogBoot(" pick=a=");
    ToyLogBootHex32((UINT32)gCodecCad);
    ToyLogBoot("\n");
    return 1;
}
