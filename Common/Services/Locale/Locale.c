/*
 * Locale.c — PR-S3-locale-1：语言状态与对外 API
 *
 * 表见 LocaleTable.c；catalog 解析见 LocaleParse.c。
 */
#include "LocalePrivate.h"
#include "Db.h"
#include "Desktop.h"
#include "Gui.h"
#include "Debug.h"

LOC_LANG gLang = LOC_LANG_EN;
char gEn[MSG_COUNT][LOCALE_STR_MAX];
char gZh[MSG_COUNT][LOCALE_STR_MAX];

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
}

void LocaleReload(void) {
    LocaleLoadCatalogs();
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
