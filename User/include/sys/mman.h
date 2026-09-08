/*
 * sys/mman.h — 匿名 mmap / munmap（PR-U-mmap；非完整 POSIX）
 */
#ifndef SYS_MMAN_H
#define SYS_MMAN_H

#include <sys/types.h>

#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4
#define PROT_NONE  0x0

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20
#define MAP_ANON      MAP_ANONYMOUS

#define MAP_FAILED ((void *)(long)-1)

/*
 * 教学子集：addr 必须为 NULL；仅 MAP_ANONYMOUS（可带 MAP_PRIVATE）；
 * fd/offset 忽略。内核在 [HalUserMmapBase, End) 分配。
 */
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int munmap(void *addr, size_t length);

#endif
