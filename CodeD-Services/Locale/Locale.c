/*
 * Locale.c — PR-S3-locale-1：语言状态与对外 API
 *
 * 表见 LocaleTable.c；catalog 解析见 LocaleParse.c。
 */
#include "LocalePrivate.h"
#include "Db.h"
#include "Desktop.h"
#include "Font.h"
#include "Gui.h"
#include "Debug.h"

LOC_LANG gLang = LOC_LANG_ZH; /* 默认汉语；切英文便宜 */
char gEn[MSG_COUNT][LOCALE_STR_MAX];
char gZh[MSG_COUNT][LOCALE_STR_MAX];

/* Worker 补预热（开机已同步扫过 gZh；此处用于 reload / 晚到 Want） */
static UINT32 gTtfPreheatIdx;
static int gTtfPreheatWant;

void LocaleTtfPreheatUi(void) {
    UINT32 i;

    for (i = 0; i < (UINT32)MSG_COUNT; i++) {
        FontTtfPreheatUtf8(gZh[i]);
    }
}

static void LocaleTtfPreheatRequest(void) {
    gTtfPreheatIdx = 0;
    gTtfPreheatWant = 1;
}

int LocaleTtfPreheatStep(void) {
    if (!gTtfPreheatWant) {
        return 0;
    }
    if (gTtfPreheatIdx >= (UINT32)MSG_COUNT) {
        gTtfPreheatWant = 0;
        return 0;
    }
    FontTtfPreheatUtf8(gZh[gTtfPreheatIdx]);
    gTtfPreheatIdx++;
    if (gTtfPreheatIdx >= (UINT32)MSG_COUNT) {
        gTtfPreheatWant = 0;
        return 0;
    }
    return 1;
}

void LocaleInitialize(void) {
    char Val[DB_VAL_MAX];

    LocaleLoadCatalogs();
    /* 缺省 / 坏值 → 汉语；仅显式 lang=en 才英文 */
    gLang = LOC_LANG_ZH;
    if (DbGet("lang", Val, sizeof(Val)) == DB_OK) {
        if (Val[0] == 'e' && Val[1] == 'n') {
            gLang = LOC_LANG_EN;
        } else if (Val[0] == 'z' && Val[1] == 'h') {
            gLang = LOC_LANG_ZH;
        }
    }
    DebugWrite("locale: ");
    DebugWrite(gLang == LOC_LANG_ZH ? "zh\n" : "en\n");
    /* 开机同步预热中文目录（FontTtfInit 已完成）；求 UI 基本无点阵 */
    LocaleTtfPreheatUi();
}

void LocaleReload(void) {
    LocaleLoadCatalogs();
    LocaleTtfPreheatUi();
    LocaleApplyUi();
}

LOC_LANG LocaleGet(void) {
    return gLang;
}

static int gLangDbFlushWant;

int LocaleDbFlushStep(void) {
    if (!gLangDbFlushWant) {
        return 0;
    }
    gLangDbFlushWant = 0;
    (void)DbEndBatch();
    return 0;
}

int LocaleSet(LOC_LANG Lang) {
    int Rc;

    if (Lang != LOC_LANG_EN && Lang != LOC_LANG_ZH) {
        return -1;
    }
    gLang = Lang;
    /* 写盘丢 Worker：Settings 点击路径 DbSave 会卡死鼠标 */
    DbBeginBatch();
    Rc = DbSet("lang", Lang == LOC_LANG_ZH ? "zh" : "en");
    gLangDbFlushWant = 1;
    if (Lang == LOC_LANG_ZH) {
        LocaleTtfPreheatRequest(); /* Worker 再扫一遍；点击路径不 sync 栅格 */
    }
    LocaleApplyUi();
    return Rc == DB_OK ? 0 : -1;
}

const char *LocStr(MSG_ID Id) {
    if ((UINT32)Id >= (UINT32)MSG_COUNT) {
        return "";
    }
    return (gLang == LOC_LANG_ZH) ? gZh[Id] : gEn[Id];
}

void LocaleApplyUi(void) {
    DesktopRefreshLabels();
    GuiRefreshTitles();
}
