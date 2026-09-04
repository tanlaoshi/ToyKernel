/*
 * HAL/Arm64/HalConsole.c — 控制台门面（串口 + 帧缓冲文字；PR-V5/V6 桌面）
 */
#include "HalConsole.h"
#include "HalSerial.h"
#include "HalVideo.h"

extern void HalCpuHalt(void);

void HalConsolePutChar(char C) {
    char Buf[2];

    Buf[0] = C;
    Buf[1] = 0;
    HalSerialWrite(Buf);
}

char HalConsoleGetChar(void) {
    while (!HalSerialDataReady()) {
        HalCpuHalt();
    }
    return HalSerialReadChar();
}

int HalConsoleHasChar(void) {
    return HalSerialDataReady();
}

int HalConsoleVideoReady(void) {
    UINT32 W;
    UINT32 H;

    HalVideoGetSize(&W, &H);
    return W != 0 && H != 0;
}

void HalConsoleWriteSerial(const char *Text) {
    HalSerialWrite(Text);
}

void HalConsoleBackspaceSerial(void) {
    HalSerialWrite("\b \b");
}

void HalConsoleDrawString(const char *Text, UINT32 Color) {
    HalVideoDrawString(Text, Color);
}

void HalConsoleDrawChar(char C, UINT32 Color) {
    HalVideoDrawChar(C, Color);
}

void HalConsoleEraseLastChar(void) {
    HalVideoEraseLastChar();
}

void HalConsoleGetTextCursor(UINT32 *X, UINT32 *Y) {
    HalVideoGetTextCursor(X, Y);
}
