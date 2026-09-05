/*
 * ctype.h — 最小字符分类（PR-L2）
 */
#ifndef CTYPE_H
#define CTYPE_H

static inline int isdigit(int c) {
    return c >= '0' && c <= '9';
}

static inline int isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static inline int isalpha(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

#endif
