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
/* COW 位布局见 HalPageIsCow / HalPageMarkCow（PR-A3） */

/* 布局见 HalUser*（PR-A11）；宏便于既有调用点 */
#define USER_CODE_VIRT  HalUserCodeVirt()
#define USER_STACK_VIRT HalUserStackVirt()
#define USER_STACK_SIZE HalUserStackSize()
#define USER_VIRT_END   HalUserVirtEnd()
/* 堆向上长到 SO 基址之前，避免盖住 USER_SO_BASE（PR-P3） */
#define USER_BRK_MAX    HalUserBrkMax()

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
