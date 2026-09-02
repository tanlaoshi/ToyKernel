/*
 * VirtualMemory.h — 虚拟内存策略层接口
 */
#ifndef VIRTUAL_MEMORY_H
#define VIRTUAL_MEMORY_H

#include "BootTypes.h"
#include "Hal.h"

#define PTE_PRESENT  HAL_PAGE_PRESENT
#define PTE_WRITABLE HAL_PAGE_WRITABLE
#define PTE_USER     HAL_PAGE_USER
/* x86 PTE 软件可用位；标记 fork 后共享、待写时复制的页 */
#define PTE_COW      (1ULL << 9)

#define USER_CODE_VIRT  0x40000000ULL
#define USER_STACK_VIRT 0x40100000ULL
#define USER_STACK_SIZE 0x4000ULL
#define USER_VIRT_END   (USER_STACK_VIRT + USER_STACK_SIZE)

#define VM_SPACE_MAX_PAGES 128

typedef struct {
    UINT64 Root;
    void   *Pages[VM_SPACE_MAX_PAGES];
    int     PageCount;
} VM_ADDR_SPACE;

int VirtualMemoryInit(void);
void VirtualMemoryEnable(void);
UINT64 VirtualMemoryKernelRoot(void);
void VirtualMemoryLoadPageTable(UINT64 Root);

int VirtualMemoryMapPage(UINT64 Virt, UINT64 Phys, UINT64 Flags);
int VirtualMemoryMapRange(UINT64 Virt, UINT64 Phys, UINTN Bytes, UINT64 Flags);

VM_ADDR_SPACE *VirtualMemorySpaceCreate(void);
void VirtualMemorySpaceDestroy(VM_ADDR_SPACE *Space);
UINT64 VirtualMemorySpaceRoot(const VM_ADDR_SPACE *Space);
int VirtualMemorySpaceMapPage(VM_ADDR_SPACE *Space, UINT64 Virt, UINT64 Phys, UINT64 Flags);
void *VirtualMemorySpaceAllocateAndTrack(VM_ADDR_SPACE *Space);

int VirtualMemoryUserAccessOk(UINT64 Virt, UINTN Len);
int VirtualMemoryCopyFromUser(void *Dst, UINT64 UserSrc, UINTN Len);
int VirtualMemoryCopyToUser(UINT64 UserDst, const void *Src, UINTN Len);

VM_ADDR_SPACE *VirtualMemorySpaceClone(VM_ADDR_SPACE *Src);

/* 缺页：写 COW 页时拆分；成功返回 0 */
int VirtualMemoryHandlePageFault(UINT64 FaultAddress, UINT64 ErrorCode);

#endif
