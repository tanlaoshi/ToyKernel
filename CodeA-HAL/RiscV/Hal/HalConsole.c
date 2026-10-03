/*
 * HAL/RiscV/HalConsole.c — 控制台门面（串口 + 帧缓冲文字；PR-V5/V6 桌面）
 */
#include "HalConsole.h"
#include "HalSerial.h"
#include "HalVideo.h"

extern void HalCpuHalt(void);

/*
 * \\n → \\r\\n。整段（或满缓冲块）一次 HalSerialWrite，与 ToyLogBoot 整行同锁粒度，
 * 避免逐字符加锁时被「Boot: iwl…」插成 lwipBoot: 。
 */
static void SerialWriteCooked(const char *Text) {
    char Buf[256];
    int N = 0;

    if (!Text) {
        return;
    }
    while (*Text) {
        if (N + 3 >= (int)sizeof(Buf)) {
            Buf[N] = 0;
            HalSerialWriteShell(Buf);
            N = 0;
        }
        if (*Text == '\r' && Text[1] == '\n') {
            Buf[N++] = '\r';
            Buf[N++] = '\n';
            Text += 2;
            continue;
        }
        if (*Text == '\n') {
            Buf[N++] = '\r';
            Buf[N++] = '\n';
            Text++;
            continue;
        }
        Buf[N++] = *Text++;
    }
    if (N > 0) {
        Buf[N] = 0;
        HalSerialWriteShell(Buf);
    }
}

void HalConsolePutChar(char C) {
    char Buf[3];

    if (C == '\n') {
        HalSerialWriteShell("\r\n");
        return;
    }
    Buf[0] = C;
    Buf[1] = 0;
    HalSerialWriteShell(Buf);
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
    HalSerialWriteShell("\b \b");
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
