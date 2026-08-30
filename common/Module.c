/*
 * Module.c — 内核模块启动器
 *
 * 遍历 MODULE 表，打印 [mod] 日志并调用各 Init 函数。
 * 设计目标：高内聚低耦合，子系统以独立模块注册即可接入。
 */
#include "Module.h"
#include "Serial.h"

/* 按顺序初始化所有模块；失败时打印模块名并返回 -1 */
int ModulesRun(const MODULE *List, int Count) {
    for (int i = 0; i < Count; i++) {
        if (i > 0) {
            SerialWrite("[mod] ");
            SerialWrite(List[i].Name);
            SerialWrite("\n");
        }
        if (List[i].Init == 0 || List[i].Init() != 0) {
            SerialWrite("[mod] ");
            SerialWrite(List[i].Name);
            SerialWrite(" failed\n");
            return -1;
        }
    }
    return 0;
}
