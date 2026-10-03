/*
 * HalFpu.c — RiscV 桩：本刀不开 FP 岛（PR-UI-ttf-fpu 仅 x86）
 */
#include "Hal.h"

void HalFpuEnableThisCpu(void) {
}

int HalFpuBegin(void) {
    return 0;
}

void HalFpuEnd(void) {
}

int HalFpuOk(void) {
    return 0;
}

int HalFpuSelfTest(void) {
    return -1;
}
