/*
 * Module.c — 内核模块启动器
 */
#include "Module.h"
#include "Hal.h"
#include "Debug.h"

static void ModLog(const char *Name, const char *Suffix) {
    HalSerialWrite("[mod] ");
    HalSerialWrite(Name);
    HalSerialWrite(Suffix);
}

/* 按顺序初始化所有模块；失败时打印模块名并返回 -1 */
int ModulesRun(const MODULE *List, int Count) {
    int i;

    for (i = 0; i < Count; i++) {
        /* 真机无 COM1 时也要走 HalSerial→GOP，才能看见卡在哪个模块 */
        ModLog(List[i].Name, "\n");
        if (List[i].Init == 0 || List[i].Init() != 0) {
            ModLog(List[i].Name, " failed\n");
            return -1;
        }
    }
    return 0;
}
