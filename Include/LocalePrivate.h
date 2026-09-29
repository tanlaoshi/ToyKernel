/*
 * LocalePrivate.h — Locale 内部分文件共用（仅 Common/Services/Locale；User 勿 include）
 *
 * 对外 API 仍在 Locale.h。
 */
#ifndef LOCALE_PRIVATE_H
#define LOCALE_PRIVATE_H

#include "Locale.h"

#define LOCALE_STR_MAX   96
#define LOCALE_FILE_MAX  (24u * 1024u)

extern LOC_LANG gLang;
extern char gEn[MSG_COUNT][LOCALE_STR_MAX];
extern char gZh[MSG_COUNT][LOCALE_STR_MAX];
extern const char *const gMsgKeys[MSG_COUNT];
extern const char *const gEnFallback[MSG_COUNT];
extern const char *const gZhFallback[MSG_COUNT];

void LocaleLoadCatalogs(void);

#endif
