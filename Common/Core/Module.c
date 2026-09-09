/*
 * Module.c — 内核模块启动器
 */
#include "Module.h"
#include "Hal.h"
#include "HalDevices.h"
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
        /* 真机无 COM1 时也要走 HalSerial→GOP，才能看见卡在哪个模块 */
        ModLog(List[i].Name, "\n");
        if (List[i].Init == 0 || List[i].Init() != 0) {
            ModLog(List[i].Name, " failed\n");
            ModProgressMark(7); /* 白条 = 失败停 */
            return -1;
        }
        /*
         * 真机 poll-USB：usb/PHOTO 之后 gui/console 初始化期间若无人 Drain，
         * 中断 IN 完成会塞满事件环 → 桌面后键鼠假死（PHOTO 时 k= 仍涨）。
         */
        if (!HalCpuIsHypervisor()) {
            HalInputPoll();
        }
    }
    return 0;
}
