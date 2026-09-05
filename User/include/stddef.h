/*
 * stddef.h — 最小 freestanding 类型（PR-L1）
 */
#ifndef STDDEF_H
#define STDDEF_H

#ifndef NULL
#define NULL ((void *)0)
#endif

typedef unsigned long size_t;
typedef long          ptrdiff_t;

#endif
