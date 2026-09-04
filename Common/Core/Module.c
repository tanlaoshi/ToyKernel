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
        /* virt：串口始终打 [mod]；x86 仍可用 DEBUG=1 看 DebugWrite */
        if (HalPlatformVirtConsole()) {
            ModLog(List[i].Name, "\n");
        } else if (i > 0) {
            DebugWrite("[mod] ");
            DebugWrite(List[i].Name);
            DebugWrite("\n");
        }
        if (List[i].Init == 0 || List[i].Init() != 0) {
            ModLog(List[i].Name, " failed\n");
            return -1;
        }
    }
    return 0;
}
