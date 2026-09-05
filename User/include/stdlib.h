#ifndef STDLIB_H
#define STDLIB_H

#include <stddef.h>
#include <toyos/syscall.h>

void *malloc(size_t n);
void free(void *p);
void exit(int status);

int abs(int x);
long labs(long x);
int atoi(const char *s);
long atol(const char *s);
void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *));

#endif
