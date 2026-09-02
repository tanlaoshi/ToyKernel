/*
 * HAL/RiscV/HalConsole.c — 控制台门面桩
 */
#include "HalConsole.h"

void HalConsolePutChar(char C) {
    (void)C;
}

char HalConsoleGetChar(void) {
    return 0;
}

int HalConsoleHasChar(void) {
    return 0;
}

int HalConsoleVideoReady(void) {
    return 0;
}

void HalConsoleWriteSerial(const char *Text) {
    (void)Text;
}

void HalConsoleBackspaceSerial(void) { }

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
