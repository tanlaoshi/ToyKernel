/*
 * LibWrite.c — PR-R4：Library 写槽
 */
#include "LibWrite.h"
#include "Hal.h"

static void (*gLibWrite)(const char *Text);

void LibWriteRegister(void (*Write)(const char *Text)) {
    gLibWrite = Write;
}

void LibWrite(const char *Text) {
    if (!Text) {
        return;
    }
    if (gLibWrite) {
        gLibWrite(Text);
        return;
    }
    HalDebugWrite(Text);
}
