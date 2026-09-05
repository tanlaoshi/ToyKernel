/*
 * Locale.h — UI 字符串表与语言切换（PR-I18N2）
 *
 * 持久化：TOYOS.DB 键 lang=en|zh（缺省 en）
 */
#ifndef LOCALE_H
#define LOCALE_H

#include "BootTypes.h"

typedef enum {
    LOC_LANG_EN = 0,
    LOC_LANG_ZH = 1
} LOC_LANG;

typedef enum {
    MSG_APP_SHELL = 0,
    MSG_APP_SETTINGS,
    MSG_APP_FILES,
    MSG_ICON_SHELL,
    MSG_ICON_SETTINGS,
    MSG_ICON_FILES,
    MSG_START,
    MSG_SET_TITLE,
    MSG_SET_MAIN,
    MSG_SET_DESKTOP_BG,
    MSG_SET_SHELL_BG,
    MSG_SET_FONT,
    MSG_SET_DISPLAY,
    MSG_SET_LANGUAGE,
    MSG_SET_HINT_MAIN,
    MSG_SET_HINT_BACK,
    MSG_SET_SAVED,
    MSG_SET_LANG_EN,
    MSG_SET_LANG_ZH,
    MSG_SET_PAGE_DESKTOP,
    MSG_SET_PAGE_SHELL,
    MSG_SET_PAGE_FONT,
    MSG_SET_PAGE_DISPLAY,
    MSG_SET_PAGE_LANG,
    MSG_FILES_VIEW,
    MSG_FILES_NEW_DIR,
    MSG_FILES_NEW_FILE,
    MSG_FILES_RENAME,
    MSG_FILES_PROMPT_HINT,
    MSG_FILES_EMPTY,
    MSG_FILES_EMPTY_HINT,
    MSG_CON_WELCOME,
    MSG_CON_READY,
    MSG_LANG_USAGE,
    MSG_LANG_NOW,
    MSG_LANG_SET,
    MSG_LANG_BAD,
    MSG_COUNT
} MSG_ID;

void LocaleInit(void);
LOC_LANG LocaleGet(void);
/* 成功 0；写盘失败仍切换内存语言并返回非 0 */
int LocaleSet(LOC_LANG Lang);
const char *LocStr(MSG_ID Id);
/* 语言变更后刷新桌面图标与窗标题（调用方负责重绘） */
void LocaleApplyUi(void);

#endif
