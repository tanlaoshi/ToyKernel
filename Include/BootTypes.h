/*
 * BootTypes.h — 架构无关的基础类型（无 UEFI / 平台语义）
 */
#ifndef BOOT_TYPES_H
#define BOOT_TYPES_H

#define NULL ((void *)0)

typedef unsigned long long  UINT64;
typedef unsigned int        UINT32;
typedef unsigned short      UINT16;
typedef unsigned char       UINT8;
typedef long long           INT64;
typedef int                 INT32;
typedef UINT64              UINTN;

#endif
