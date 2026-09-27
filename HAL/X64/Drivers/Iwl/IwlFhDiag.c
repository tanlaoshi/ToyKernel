/*
 * IwlFhDiag.c — FH 可达性探针 + 失败黄字（PR-N-wifi-2 短刀）
 *
 * 注意：0xFFC 等空洞读 A5A5 是正常「未实现」，不能当页界判据。
 * 对照：h=HBUS(0x444) 应活；f=FH@0x1000 / c=TCSR 若死 = FH 门控。
 */
#include "IwlPrivate.h"
#include "HalSerial.h"

#define IWL_FH_MEM_LO   0x1000u

static void IwlPutHex8(char *Line, int *N, UINT32 V) {
    char Hex[12];
    HalSerialFormatHex(Hex, V, 8);
    Line[(*N)++] = Hex[2];
    Line[(*N)++] = Hex[3];
    Line[(*N)++] = Hex[4];
    Line[(*N)++] = Hex[5];
    Line[(*N)++] = Hex[6];
    Line[(*N)++] = Hex[7];
    Line[(*N)++] = Hex[8];
    Line[(*N)++] = Hex[9];
}

int IwlFhAliveVal(UINT32 V) {
    return ((V & ~0xFu) != 0xA5A5A5A0u) && ((V & ~0xFu) != 0x5A5A5A50u);
}

void IwlFhProbeAccess(const char *Tag) {
    char Line[96];
    int n = 0;
    UINT32 Hbus;
    UINT32 FhLo;
    UINT32 Tcsr;
    const char *T = Tag ? Tag : "fhacc";

    while (*T && n < 16) {
        Line[n++] = *T++;
    }
    if (!IwlNicLock()) {
        Line[n++] = ' ';
        Line[n++] = 'l';
        Line[n++] = 'o';
        Line[n++] = 'c';
        Line[n++] = 'k';
        Line[n++] = '-';
        Line[n] = 0;
        IwlLogStage(Line);
        return;
    }
    Hbus = IwlMmioR32(IWL_HBUS_TARG_PRPH_WADDR);
    FhLo = IwlMmioR32(IWL_FH_MEM_LO);
    Tcsr = IwlMmioR32(IWL_FH_TCSR_CONFIG_9);
    IwlNicUnlock();

    Line[n++] = ' '; Line[n++] = 'h'; Line[n++] = '=';
    IwlPutHex8(Line, &n, Hbus);
    Line[n++] = ' '; Line[n++] = 'f'; Line[n++] = '=';
    IwlPutHex8(Line, &n, FhLo);
    Line[n++] = ' '; Line[n++] = 'c'; Line[n++] = '=';
    IwlPutHex8(Line, &n, Tcsr);
    if (!IwlFhAliveVal(FhLo) || !IwlFhAliveVal(Tcsr)) {
        Line[n++] = ' '; Line[n++] = 'd'; Line[n++] = 'e';
        Line[n++] = 'a'; Line[n++] = 'd';
    }
    if (IwlFhAliveVal(Hbus) && !IwlFhAliveVal(FhLo)) {
        Line[n++] = ' '; Line[n++] = 'f'; Line[n++] = 'h';
        Line[n++] = 'g'; Line[n++] = 'a'; Line[n++] = 't'; Line[n++] = 'e';
    }
    Line[n] = 0;
    /* #129：FH 探针仅异常时亮黄字 */
    if (!IwlFhAliveVal(FhLo) || !IwlFhAliveVal(Tcsr) || !IwlFhAliveVal(Hbus)) {
        IwlLogStage(Line);
    } else {
        IwlLogVerb(Line);
    }
}

void IwlFhLogFail(int Why, UINT32 Dst, UINT32 Phys, UINT32 Tssr, UINT32 Tcsr) {
    char Line[96];
    int n = 0;
    const char *W = (Why == 1) ? "niclock"
                  : (Why == 2) ? "fhtx"
                  : (Why == 3) ? "oom" : "fail";
    while (*W && n < 28) {
        Line[n++] = *W++;
    }
    Line[n++] = ' '; Line[n++] = 'd'; Line[n++] = '=';
    IwlPutHex8(Line, &n, Dst);
    Line[n++] = ' '; Line[n++] = 'p'; Line[n++] = '=';
    IwlPutHex8(Line, &n, Phys);
    Line[n] = 0;
    IwlLogStage(Line);
    n = 0;
    Line[n++] = 't'; Line[n++] = '=';
    IwlPutHex8(Line, &n, Tssr);
    Line[n++] = ' '; Line[n++] = 'c'; Line[n++] = '=';
    IwlPutHex8(Line, &n, Tcsr);
    Line[n] = 0;
    IwlLogStage(Line);
}
