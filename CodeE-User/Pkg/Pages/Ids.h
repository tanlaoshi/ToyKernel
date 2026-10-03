/*
 * Ids.h — 多页面策略 A：逻辑 ID 按页分区间；导航 9000 独立
 * Home 1000–1099；Settings 1100–1199；导航 9000–9099。
 */
#ifndef PAGES_IDS_H
#define PAGES_IDS_H

enum {
    PAGE_HOME = 0,
    PAGE_SETTINGS = 1,
    PAGE_N = 2
};

#define ID_HOME_BASE     1000
#define ID_HOME_PING     1000

#define ID_SET_BASE 1100
#define ID_SET_OK   1100

#define ID_NAV_BASE     9000
#define ID_NAV_HOME     9000
#define ID_NAV_SETTINGS 9001

#endif
