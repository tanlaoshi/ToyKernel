/*
 * Module.c — 内核模块启动器
 */
#include "Module.h"
#include "Hal.h"
#include "HalVideo.h"
#include "Debug.h"

static void ModLog(const char *Name, const char *Suffix) {
    HalSerialWrite("[mod] ");
    HalSerialWrite(Name);
    HalSerialWrite(Suffix);
}

/* 真机 bring-up：进度改走黄字 boot log，不再画左上角色块 */
static void ModProgressMark(int Step) {
    (void)Step;
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
            ModProgressMark(7); /* 白条 = 失败停 */
            return -1;
        }
        ModProgressMark(i);
    }
    return 0;
}
