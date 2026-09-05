/*
 * toyos/version.h — CRT / libtoyos 版本（PR-L1）
 * 与 syscall 号无关；改 ABI 时递增 MINOR，破坏性改 MAJOR。
 */
#ifndef TOYOS_VERSION_H
#define TOYOS_VERSION_H

#define TOYOS_CRT_VERSION_MAJOR 1
#define TOYOS_CRT_VERSION_MINOR 1
#define TOYOS_CRT_VERSION_PATCH 0

#define TOYOS_CRT_VERSION_STRING "1.1.0"

#endif
