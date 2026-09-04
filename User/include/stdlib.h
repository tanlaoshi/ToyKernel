#ifndef STDLIB_H
#define STDLIB_H

#include "toy_syscall.h"

void *malloc(size_t n);
void free(void *p);
void exit(int status);

#endif
