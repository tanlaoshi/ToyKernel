/*
 * IwlLog.c — iwl 黄字（PR-S-iwl-split-2，自 Iwl.c 搬家）
 */
#include "IwlPrivate.h"
#include "HalSerial.h"
#include "ToySerialLog.h"

static void IwlAppend(char *Line, int *N, int Max, const char *S) {
    while (*S && *N < Max) {
        Line[(*N)++] = *S++;
    }
}

void IwlLogBound(void) {
    char Line[120];
    char Hex[12];
    int n = 0;

    IwlAppend(Line, &n, 100, "Boot: iwl8265 ");
    Line[n++] = 'b';
    Line[n++] = '=';
    HalSerialFormatHex(Hex, gIwlBus, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = ':';
    HalSerialFormatHex(Hex, gIwlDev, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = '.';
    HalSerialFormatHex(Hex, gIwlFn, 1);
    Line[n++] = Hex[2];
    Line[n++] = ' ';
    IwlAppend(Line, &n, 110, gIwlFwOk ? "fw=ok" : "fw=miss");
    Line[n++] = ' ';
    IwlAppend(Line, &n, 110, gIwlBarOk ? "bar=ok" : "bar=-");
    Line[n++] = ' ';
    IwlAppend(Line, &n, 110, gIwlAlive ? "alive=ok" : "alive=-");
    if (gIwlSsidOk) {
        Line[n++] = ' ';
        IwlAppend(Line, &n, 110, "ssid=ok");
    }
    if (gIwlAssociated) {
        Line[n++] = ' ';
        IwlAppend(Line, &n, 110, "assoc=ok");
    }
    if (gIwlWpa2Ok) {
        Line[n++] = ' ';
        IwlAppend(Line, &n, 110, "wpa2=ok");
    }
    Line[n++] = '\n';
    Line[n] = 0;
    ToyLogBoot(Line);
}

/* 安静默认：成功路径只留 IwlLogBound；异常 + 手测关键黄字仍出；VERBOSE=1 全开 */
static int IwlTagAlert(const char *Tag) {
    static const char *const Keys[] = {
        "fail", "miss", "soft", "stall", "bad", "nocfg",
        "deauth", "disassoc", "oom", "=none", "=disc",
        "=norsn", "=wpa3", "=to", "=skip", "gtk=no", "bar=novm",
        /* 手测：TKIP/组播/DHCP Offer（Bound 一行不够验收） */
        "rx=tkip", "rx=mic", "rx=off", "gtk=", "assoc=gc=",
        0
    };
    int i;

    if (!Tag) {
        return 0;
    }
    for (i = 0; Keys[i]; i++) {
        const char *K = Keys[i];
        const char *P = Tag;
        while (*P) {
            const char *A = P;
            const char *B = K;
            while (*A && *B && *A == *B) {
                A++;
                B++;
            }
            if (!*B) {
                return 1;
            }
            P++;
        }
    }
    return 0;
}

static void IwlLogEmit(const char *Tag) {
    char Line[80];
    int n = 0;
    IwlAppend(Line, &n, 70, "Boot: iwl8265 ");
    IwlAppend(Line, &n, 70, Tag);
    Line[n++] = '\n';
    Line[n] = 0;
    ToyLogBoot(Line);
}

void IwlLogStage(const char *Tag) {
#if IWL_LOG_VERBOSE
    IwlLogEmit(Tag);
#else
    if (IwlTagAlert(Tag)) {
        IwlLogEmit(Tag);
    }
#endif
}

void IwlLogVerb(const char *Tag) {
#if IWL_LOG_VERBOSE
    IwlLogEmit(Tag);
#else
    (void)Tag;
#endif
}
