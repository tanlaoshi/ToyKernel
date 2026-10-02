/*
 * sys/types.h — 用户态基础类型（PR-L1 + 刀 C socklen）
 */
#ifndef SYS_TYPES_H
#define SYS_TYPES_H

#include <stddef.h>

typedef long          ssize_t;
typedef long          pid_t;
typedef long          off_t;
typedef unsigned int  mode_t;

typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef unsigned short sa_family_t;
typedef unsigned int   socklen_t;

#endif
