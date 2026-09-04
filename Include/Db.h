/*
 * Db.h — 轻量 KV 库（PR-DB1）
 *
 * 卷上文件 TOYOS.DB（默认卷）：每行 key=value；# 注释。
 * Theme 的 desktop/shell/font/mode 经此持久化；仍可从 THEME.CFG 导入。
 */
#ifndef DB_H
#define DB_H

#include "BootTypes.h"

#define DB_PATH       "TOYOS.DB"
#define DB_KEY_MAX    32
#define DB_VAL_MAX    64
#define DB_MAX_RECORDS 64

/* 成功 0；失败负值（与 Fat 风格一致，也可用 -1） */
#define DB_OK     0
#define DB_ERR   (-1)
#define DB_NOENT (-2)
#define DB_FULL  (-3)
#define DB_INVAL (-4)

/* 读盘或空库；可重复调用。注册 dbget/dbset/dblist */
int DbInit(void);

int DbGet(const char *Key, char *Out, UINTN OutMax);
/* 内存更新并立刻刷盘 */
int DbSet(const char *Key, const char *Value);
int DbDelete(const char *Key);
int DbCount(void);
/* 按序回调已有记录；Cb 返回非 0 则中止 */
int DbForEach(int (*Cb)(const char *Key, const char *Value, void *Ctx), void *Ctx);

int DbLoad(void);
int DbSave(void);

#endif
