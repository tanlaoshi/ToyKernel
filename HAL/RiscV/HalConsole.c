/*
 * HAL/RiscV/HalConsole.c — 控制台门面桩
 */
#include "HalConsole.h"
#include "HalSerial.h"

void HalConsolePutChar(char C) {
    char Buf[2];

    Buf[0] = C;
    Buf[1] = 0;
    HalSerialWrite(Buf);
}

char HalConsoleGetChar(void) {
    return HalSerialDataReady() ? HalSerialReadChar() : 0;
}

int HalConsoleHasChar(void) {
    return HalSerialDataReady();
}

int HalConsoleVideoReady(void) {
    return 0;
}

void HalConsoleWriteSerial(const char *Text) {
    HalSerialWrite(Text);
}

void HalConsoleBackspaceSerial(void) {
    HalSerialWrite("\b \b");
}

void HalConsoleDrawString(const char *Text, UINT32 Color) {
    (void)Text;
    (void)Color;
}

void HalConsoleDrawChar(char C, UINT32 Color) {
    (void)C;
    (void)Color;
}

void HalConsoleEraseLastChar(void) { }

void HalConsoleGetTextCursor(UINT32 *X, UINT32 *Y) {
    if (X) {
        *X = 0;
    }
    if (Y) {
        *Y = 0;
    }
}
