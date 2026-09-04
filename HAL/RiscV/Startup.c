/*
 * Startup.c — PR-A6 bringup 主逻辑（入口在 Startup.S）
 */
#include "HalSerial.h"
#include "Hal.h"

void BringupMain(void) {
    HalSerialInit();
    HalSerialWrite("ToyOS RiscV virt: hello\n");
    HalCpuHalt();
}
