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

void IwlLogStage(const char *Tag) {
    char Line[80];
    int n = 0;
    IwlAppend(Line, &n, 70, "Boot: iwl8265 ");
    IwlAppend(Line, &n, 70, Tag);
    Line[n++] = '\n';
    Line[n] = 0;
    ToyLogBoot(Line);
}

void IwlLogVerb(const char *Tag) {
#if IWL_LOG_VERBOSE
    IwlLogStage(Tag);
#else
    (void)Tag;
#endif
}
