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

LOC_LANG gLang = LOC_LANG_EN;
char gEn[MSG_COUNT][LOCALE_STR_MAX];
char gZh[MSG_COUNT][LOCALE_STR_MAX];

/* PR-UI-ttf-3：Worker 分片预热；不挡 Gui/鼠标 */
static UINT32 gTtfPreheatIdx;
static int gTtfPreheatWant;

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
    gLang = LOC_LANG_EN;
    if (DbGet("lang", Val, sizeof(Val)) == DB_OK) {
        if (Val[0] == 'z' && Val[1] == 'h') {
            gLang = LOC_LANG_ZH;
        } else if (Val[0] == 'e' && Val[1] == 'n') {
            gLang = LOC_LANG_EN;
        }
    }
    DebugWrite("locale: ");
    DebugWrite(gLang == LOC_LANG_ZH ? "zh\n" : "en\n");
    LocaleTtfPreheatRequest();
}

void LocaleReload(void) {
    LocaleLoadCatalogs();
    LocaleTtfPreheatRequest();
    LocaleApplyUi();
}

LOC_LANG LocaleGet(void) {
    return gLang;
}

int LocaleSet(LOC_LANG Lang) {
    int Rc;

    if (Lang != LOC_LANG_EN && Lang != LOC_LANG_ZH) {
        return -1;
    }
    gLang = Lang;
    Rc = DbSet("lang", Lang == LOC_LANG_ZH ? "zh" : "en");
    if (Lang == LOC_LANG_ZH) {
        LocaleTtfPreheatRequest();
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
