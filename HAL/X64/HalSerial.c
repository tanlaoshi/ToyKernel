/*
 * HAL/X86_64/HalSerial.c — 串口门面，委托 COM1 驱动
 */
#include "HalSerial.h"
#include "Serial.h"

void HalSerialInit(void) {
    SerialInit();
}

void HalSerialWrite(const char *Text) {
    SerialWrite(Text);
}

int HalSerialDataReady(void) {
    return SerialDataReady();
}

char HalSerialReadChar(void) {
    return SerialReadChar();
}

void HalSerialHexFormat(char *Buf, UINT64 Value, int Digits) {
    SerialHexFormat(Buf, Value, Digits);
}
