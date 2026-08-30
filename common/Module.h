/*
 * Module.h — 可插拔模块框架
 *
 * 每个子系统注册为 MODULE（名称 + Init 函数），由 ModulesRun 按表顺序初始化。
 */
#ifndef MODULE_H
#define MODULE_H

typedef struct {
    const char *Name;   /* 模块名，用于启动日志 */
    int (*Init)(void);  /* 初始化函数，0 成功，非 0 失败 */
} MODULE;

/* 顺序执行模块列表，任一失败则返回 -1 */
int ModulesRun(const MODULE *List, int Count);

#endif
