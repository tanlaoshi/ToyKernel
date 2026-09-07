/*
 * HAL/X64/HalSerial.c — 串口门面；PR-H3：无 COM1 时镜像到 GOP 文本
 */
#include "HalSerial.h"
#include "HalVideo.h"
#include "Serial.h"

#define GOP_RING 4096

static char gRing[GOP_RING];
static UINTN gRingLen;
static int gVideoUp;
static int gGopBanner;

static void RingAppend(const char *Text) {
    while (Text && *Text) {
        if (gRingLen + 1 >= GOP_RING) {
            /* 丢最旧半段，保留近期 boot 日志 */
            UINTN Keep = GOP_RING / 2;
            UINTN i;
            for (i = 0; i < Keep; i++) {
                gRing[i] = gRing[gRingLen - Keep + i];
            }
            gRingLen = Keep;
        }
        gRing[gRingLen++] = *Text++;
    }
}

static void GopWrite(const char *Text) {
    if (!Text || !*Text) {
        return;
    }
    if (!gGopBanner) {
        HalVideoDrawString("ToyOS GOP console (no COM1)\n", 0x00FFFF00u);
        gGopBanner = 1;
    }
    HalVideoDrawString(Text, 0x00FFFFFFu);
    HalVideoPresent();
}

void HalSerialInit(void) {
    SerialInit();
    gRingLen = 0;
    gVideoUp = 0;
    gGopBanner = 0;
    if (!SerialPresent()) {
        RingAppend("boot: no COM1; GOP console pending video\n");
    } else {
        SerialWrite("boot: COM1 serial ok\n");
    }
}

int HalSerialPresent(void) {
    return SerialPresent();
}

void HalSerialGopEnable(void) {
    if (SerialPresent()) {
        return;
    }
    gVideoUp = 1;
    if (gRingLen > 0) {
        gRing[gRingLen] = '\0';
        GopWrite(gRing);
        gRingLen = 0;
    }
}

void HalSerialWrite(const char *Text) {
    if (!Text) {
        return;
    }
    if (SerialPresent()) {
        SerialWrite(Text);
        return;
    }
    if (gVideoUp) {
        GopWrite(Text);
    } else {
        RingAppend(Text);
    }
}

int HalSerialDataReady(void) {
    return SerialDataReady();
}

char HalSerialReadChar(void) {
    return SerialReadChar();
}

void HalSerialFormatHex(char *Buf, UINT64 Value, int Digits) {
    SerialHexFormat(Buf, Value, Digits);
}
