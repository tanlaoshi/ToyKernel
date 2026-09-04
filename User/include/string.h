#ifndef STRING_H
#define STRING_H

#include "toy_syscall.h"

size_t strlen(const char *s);
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);

#endif
