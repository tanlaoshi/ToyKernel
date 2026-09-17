/*
 * Store.h — 本地商店（PR-S1～S5）+ 依赖（PR-M1）+ 组合装卸（PR-M2）
 */
#ifndef STORE_H
#define STORE_H

#include "BootTypes.h"

#define STORE_CATALOG_PATH   "Assets/Store/catalog.txt"
#define STORE_CATALOG_ALT    "Store/catalog.txt"
#define STORE_APPS_DIR       "Apps"
#define STORE_FONTS_DIR      "Assets/Fonts"
#define STORE_PACKS_DIR      "Assets/Packs"
#define STORE_ID_MAX         32
#define STORE_FILE_MAX       64
#define STORE_TITLE_MAX      48
#define STORE_ARCH_MAX       16
#define STORE_DEPENDS_MAX    64 /* 与 DB_VAL_MAX 对齐；逗号分隔 id */
#define STORE_ENTRIES_MAX    32
#define STORE_INSTALLED_MAX  24

typedef struct STORE_ENTRY {
    char Id[STORE_ID_MAX];
    char Type[12];
    UINT32 Version;
    char File[STORE_FILE_MAX];
    char Sha256[72]; /* "-" / 8hex FNV / 跳过其它 */
    char Arch[STORE_ARCH_MAX];
    char Title[STORE_TITLE_MAX];
    char Depends[STORE_DEPENDS_MAX]; /* PR-M1：可选；空或 "-" = 无依赖 */
} STORE_ENTRY;

/* PR-S4：已装项（ToyDB si.<id>=type|file） */
typedef struct STORE_INSTALLED {
    char Id[STORE_ID_MAX];
    char Type[12];
    char File[STORE_FILE_MAX];
} STORE_INSTALLED;

/* 加载 catalog；优先 Store/（S2 同步后），再 Assets/；成功返回条目数 */
int StoreLoadCatalog(STORE_ENTRY *Out, int Max, int *OutCount);

/* 按 id 安装：app→Apps/；font→Assets/Fonts/；asset→Assets/Packs/；并记清单 */
int StoreInstall(const char *Id);

/* PR-M2：按依赖顺序装齐「功能」（缺依赖先装，再装 Id）；单包仍可用 StoreInstall */
int StoreComboInstall(const char *Id);

/* PR-S4：列已装 / 卸载（删载荷 + 清 ToyDB） */
int StoreListInstalled(STORE_INSTALLED *Out, int Max, int *OutCount);
int StoreRemove(const char *Id);

/* PR-M2：卸 Id，再卸其依赖中已无引用者（逆序组合拆卸） */
int StoreComboRemove(const char *Id);

/* PR-S5：可见性 — 是否已装 / 依赖串（sd.<id>，缺省 "-"） */
int StoreIsInstalled(const char *Id);
/* 批量填已装标志：每目录只 List 一次（UI 刷新用，避免 N 次扫盘） */
void StoreFillInstalledFlags(const STORE_ENTRY *Tab, int Count, int *OutFlags);
int StoreGetDepends(const char *Id, char *Out, int OutMax);

/*
 * 商店托管载荷：Apps 下 catalog/si 登记的 .ELF，或 Fonts/Packs 中已登记项。
 * 文档/数据（README、THEME、用户文件等）返回 0，可直接删。
 * 安装包约定：装入须登记 si.*；自带 uninstall 卸完后须 StoreUnregister。
 */
int StoreIsManagedPayload(const char *Path);
/* Store 内部删托管文件（绕过 FileSystem 保护） */
int StoreDeleteManagedFile(const char *Path);
/* 仅清清单（文件已由卸载程序删掉）；成功 FAT_OK */
int StoreUnregister(const char *Id);
/* FileSystem 删/改名前查询：1=允许绕过托管保护（Store 内部） */
int StorePayloadBypassActive(void);

/* 当前本机 arch 标签（如 x86_64） */
const char *StoreHostArch(void);

/* PR-S2：仓库 ip:port（ToyDB store.repo=；默认 10.0.2.2:8080） */
void StoreRepoLoadFromDb(void);
int  StoreRepoSet(const char *IpPort);
void StoreRepoGet(UINT32 *OutIp, UINT16 *OutPort);

/* GET → 写 DestRel；ExpectHash "-" 跳过；成功 0，HTTP 非200 → -2，哈希 → -3 */
int StoreFetchPath(const char *UrlPath, const char *DestRel, const char *ExpectHash);
int StoreSyncCatalog(void);
int StoreFetchId(const char *Id);

/* 内核任务栈仅 8KiB；list 用 BSS scratch，勿在栈上开 STORE_ENTRY[N] */
STORE_ENTRY *StoreScratchTab(void);

#endif
