/*
 * IwlCfg.c — 读 FW/WIFI.CFG（SSID= / PSK=）；永不日志 PSK
 */
#include "IwlPrivate.h"
#include "FileSystem.h"
#include "Fat.h"

char gIwlSsid[IWL_SSID_MAX + 1];
char gIwlPsk[IWL_PSK_MAX + 1];
UINT8 gIwlPmk[32];
int gIwlPmkOk;

static int IwlIsSpace(char C) {
    return C == ' ' || C == '\t' || C == '\r' || C == '\n';
}

static void IwlCopyKey(char *Dst, int Max, const char *Src, int Len) {
    int i;
    int n = 0;

    while (Len > 0 && IwlIsSpace(Src[Len - 1])) {
        Len--;
    }
    for (i = 0; i < Len && n < Max; i++) {
        if (Src[i] == 0) {
            break;
        }
        Dst[n++] = Src[i];
    }
    Dst[n] = 0;
}

int IwlPmkPrepare(void) {
    UINTN SsidLen = 0;
    UINTN i;

    gIwlPmkOk = 0;
    for (i = 0; i < 32; i++) {
        gIwlPmk[i] = 0;
    }
    if (!gIwlPsk[0] || !gIwlSsid[0]) {
        return 0;
    }
    while (gIwlSsid[SsidLen]) {
        SsidLen++;
    }
    if (!IwlPbkdf2Sha1(gIwlPsk, (const UINT8 *)gIwlSsid, SsidLen, 4096, gIwlPmk, 32)) {
        return 0;
    }
    gIwlPmkOk = 1;
    IwlLogVerb("pmk=ok");
    return 1;
}

int IwlCfgLoad(void) {
    char Buf[256];
    UINTN Sz = 0;
    UINTN i;
    int Err;

    gIwlSsid[0] = 0;
    gIwlPsk[0] = 0;
    for (i = 0; i < sizeof(Buf); i++) {
        Buf[i] = 0;
    }
    Err = FileSystemReadFile(IWL_CFG_PATH, Buf, sizeof(Buf) - 1, &Sz);
    if (Err != FAT_OK || Sz == 0) {
        return 0;
    }
    Buf[Sz] = 0;
    i = 0;
    while (i < Sz) {
        UINTN Line = i;
        UINTN End;
        while (i < Sz && Buf[i] != '\n') {
            i++;
        }
        End = i;
        if (i < Sz && Buf[i] == '\n') {
            i++;
        }
        while (Line < End && IwlIsSpace(Buf[Line])) {
            Line++;
        }
        if (Line + 5 <= End && Buf[Line] == 'S' && Buf[Line + 1] == 'S'
            && Buf[Line + 2] == 'I' && Buf[Line + 3] == 'D' && Buf[Line + 4] == '=') {
            IwlCopyKey(gIwlSsid, IWL_SSID_MAX, Buf + Line + 5, (int)(End - Line - 5));
        } else if (Line + 4 <= End && Buf[Line] == 'P' && Buf[Line + 1] == 'S'
                   && Buf[Line + 2] == 'K' && Buf[Line + 3] == '=') {
            IwlCopyKey(gIwlPsk, IWL_PSK_MAX, Buf + Line + 4, (int)(End - Line - 4));
        }
    }
    return gIwlSsid[0] != 0 && gIwlPsk[0] != 0;
}
