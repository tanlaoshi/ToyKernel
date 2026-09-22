/*
 * StoreUiPrivate.h — StoreUi 内部共享头（仅 Common/Services/StoreUi 使用）
 *
 * 禁止 User 程序、HAL、Core 包含本文件。
 * 源文件在 Common/Services/StoreUi/（核心 StoreUi.c）。
 * 对外 API 仍在 StoreUi.h。
 */
#ifndef STORE_UI_PRIVATE_H
#define STORE_UI_PRIVATE_H

#include "StoreUi.h"
#include "StoreJob.h"
#include "Store.h"
#include "Desktop.h"
#include "Gui.h"
#include "GuiPrivate.h"
#include "HalVideo.h"
#include "Font.h"
#include "Locale.h"
#include "Theme.h"
#include "Debug.h"
#include "UI.h"

/* ===== 宏（从 StoreUi.c 搬入；值不变） ===== */
#define STORE_SIDE_W    128u
#define STORE_SIDE_BG   0x00A0A8B0u
#define STORE_PREV_BG   0x00D8D8E0u
#define STORE_SB_W      12u
#define STORE_BTN_H     28u
#define STORE_BTN_GAP   10u
#define STORE_BTN_N     3
#define STORE_MAP_MAX   STORE_ENTRIES_MAX

/* ===== 类型（布局不变） ===== */
typedef enum {
    STORE_CAT_ALL = 0,
    STORE_CAT_APP,
    STORE_CAT_FONT,
    STORE_CAT_ASSET,
    STORE_CAT_INSTALLED,
    STORE_CAT_COUNT
} STORE_CAT;

/* ===== 全局（定义在 StoreUi.c） ===== */
extern int gStoreUiCat; /* 与 SettingsUi 的 gCat 区分 */
extern int gSel;
extern int gStoreUiScroll; /* 与 FilesUi 的 gScroll 区分 */
extern int gFiltCount;
extern int gMap[STORE_MAP_MAX];
extern int gInstCache[STORE_ENTRIES_MAX];
extern int gCatalogN;
extern char gStoreUiStatus[80]; /* 与 FilesUi 的 gStatus 区分 */

extern UINT32 gStoreUiSideX, gStoreUiSideW, gStoreUiSideRow0, gStoreUiSideLineH;
extern UINT32 gListX, gStoreUiListTop, gStoreUiListRowW, gStoreUiListLineH;
extern int gStoreUiListVisible;
extern UINT32 gStoreUiSbX, gStoreUiSbY, gStoreUiSbW, gStoreUiSbH;
extern int gStoreUiSbVisible;
extern UINT32 gStoreUiPrevX, gStoreUiPrevW;
extern UINT32 gBtnY, gBtnW, gBtnX0;
extern int gHoverSide;
extern int gHoverRow;
extern int gHoverBtn;
extern int gPressBtn;

extern const char *const gBtnLabel[STORE_BTN_N];

/* Console / Store 已有同名符号，这里不能再导出。 */
static inline int StrEq(const char *A, const char *B) {
    if (!A || !B) {
        return 0;
    }
    while (*A && *A == *B) {
        A++;
        B++;
    }
    return *A == 0 && *B == 0;
}

/* ===== StoreUiModel.c ===== */
const char *StoreCatLabel(int C);
int CachedInstalled(int CatalogIdx);
void StoreSetStatus(const char *S);
void RebuildFilter(void);
void Reload(void);
STORE_ENTRY *SelectedEntry(void);
void StoreBtnGeom(UINT32 ListX, UINT32 ListW);
void ClampScroll(void);

/* ===== StoreUiPaint.c ===== */
void StorePaintList(void);

/* ===== StoreJobStatus.c ===== */
void StoreJobStatusWithId(const char *Verb, const char *Id);
void StoreJobStatusProgress(const char *Verb, const char *Id, int Cur, int Total);
void StoreJobStatusCopy(const char *Id, UINTN Got, UINTN Size);
void StoreJobBusyRepaint(void);
void StoreJobFinishStatus(STORE_JOB_KIND Kind, int Err, int PlanN);

#endif
