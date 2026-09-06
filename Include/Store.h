/*
 * Store.h — 本地/离线商店（PR-S1）：读 catalog → 复制 ELF 到 Apps/
 */
#ifndef STORE_H
#define STORE_H

#include "BootTypes.h"

#define STORE_CATALOG_PATH   "Assets/Store/catalog.txt"
#define STORE_CATALOG_ALT    "Store/catalog.txt"
#define STORE_APPS_DIR       "Apps"
#define STORE_ID_MAX         32
#define STORE_FILE_MAX       64
#define STORE_TITLE_MAX      48
#define STORE_ARCH_MAX       16
#define STORE_ENTRIES_MAX    32

typedef struct STORE_ENTRY {
    char Id[STORE_ID_MAX];
    char Type[12];
    UINT32 Version;
    char File[STORE_FILE_MAX];
    char Sha256[72]; /* "-" or hex */
    char Arch[STORE_ARCH_MAX];
    char Title[STORE_TITLE_MAX];
} STORE_ENTRY;

/* 加载 catalog；成功返回条目数，失败负值 */
int StoreLoadCatalog(STORE_ENTRY *Out, int Max, int *OutCount);

/* 按 id 安装 type=app 到 Apps/<file>；成功 0 */
int StoreInstall(const char *Id);

/* 当前本机 arch 标签（如 x86_64） */
const char *StoreHostArch(void);

#endif
