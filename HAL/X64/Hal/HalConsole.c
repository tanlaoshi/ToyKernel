/*
 * HAL/X86_64/HalConsole.c — 控制台门面（串口 + 帧缓冲文字）
 */
#include "HalConsole.h"
#include "HalSerial.h"
#include "HalVideo.h"

extern void HalCpuHalt(void);

/* \\n → \\r\\n，避免主机终端只认 CR 时「后一行盖前一行」；已有 \\r\\n 不叠成 \\r\\r\\n */
static void SerialWriteCooked(const char *Text) {
    char One[2];

    if (!Text) {
        return;
    }
    One[1] = 0;
    while (*Text) {
        if (*Text == '\r' && Text[1] == '\n') {
            HalSerialWrite("\r\n");
            Text += 2;
            continue;
        }
        if (*Text == '\n') {
            HalSerialWrite("\r\n");
            Text++;
            continue;
        }
        One[0] = *Text++;
        HalSerialWrite(One);
    }
}

void HalConsolePutChar(char C) {
    char Buf[2];

    if (C == '\n') {
        HalSerialWrite("\r\n");
        return;
    }
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
    SerialWriteCooked(Text);
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
