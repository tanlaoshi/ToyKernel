#ifndef STDIO_H
#define STDIO_H

#include <stdarg.h>
#include <stddef.h>

int printf(const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int sprintf(char *buf, const char *fmt, ...);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int vsprintf(char *buf, const char *fmt, va_list ap);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int puts(const char *s);
void perror(const char *s);

#endif
